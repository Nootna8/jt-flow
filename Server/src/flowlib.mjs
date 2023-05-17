import * as ffi from "ffi-napi";
import * as ref from "ref-napi";
import * as StructDi from "ref-struct-di";
import tempfile from "tempfile";
import * as zlib from "zlib";
import { modelIds, blockdb, ipfs } from "./db.mjs";

const StructType = StructDi.default(ref);

var FrameNumberType = ref.types.ulong;

const FlowPropertiesStruct = StructType({
    numberOfPools: ref.types.int,
    maxValue: ref.types.float,
    overlayHalf: ref.types.bool,
    focusPoint: ref.types.float,
    focusSize: ref.types.float,
    waveSmoothing1: ref.types.float,
});

var FrameRangeStruct = StructType({
    fromFrame: FrameNumberType,
    toFrame: FrameNumberType,
});

const ActionStruct = StructType({
    timeFrame: ref.types.int,
    actionPos: ref.types.int,
});
const ActionStructPtr = ref.refType(ActionStruct);

var FlowProperties = new FlowPropertiesStruct({
    numberOfPools: 180,
    maxValue: 0.2,
    overlayHalf: false,
    focusPoint: 0.5,
    focusSize: 0.5,
    waveSmoothing1: 0.5,
});

var FlowPropertiesPtr = ref.refType(FlowPropertiesStruct);

// var lib = env.FLOWLIB || '/app/FlowLib/build/libJTFlowLav'
export var libFile =
    "C:/dev/JackerTracker/JTFlow/FlowLib/build/Release/JTFlowCuda.dll";

try {
    var flowLib = ffi.Library(libFile, {
        FlowCreateHandle: ["pointer", ["string", FlowPropertiesPtr]],
        FlowDestroyHandle: ["bool", ["pointer"]],
        FlowRun: ["bool", ["pointer", "pointer", "int"]],
        FlowGetLength: [FrameNumberType, ["pointer"]],
        FlowGetLengthMs: [FrameNumberType, ["pointer"]],
        // 'FlowSave': ['bool', ['pointer', 'string']],
        FlowCalcWave: ["bool", ["pointer", FrameRangeStruct, "pointer", "pointer"]],
        FlowGetData: ["bool", ["pointer", FrameRangeStruct, "pointer"]],
        FlowLastError: ["string", []],
        FlowSetLogger: ["bool", ["pointer"]],
    });
} catch (e) {
    console.log("Library error", e);
    process.exit(1);
}

function callFlowLib(result) {
    if (!result) {
        var error = flowLib.FlowLastError();
        console.log("Error falsy result: ", error, "|");
        throw new Error(error);
    }

    return result;
}

var logCallback = ffi.Callback(
    "void",
    ["int", "string"],
    function (level, message) {
        console.log("FlowLib: ", message);
    }
);

// callFlowLib(flowLib.FlowSetLogger(logCallback));

// 180 angle windows * 4 btyes / 1024 = 85kb per frame
var flowBlockFrames = 400;
var blockRowSize = 180 * 4;
var blockSize = blockRowSize * flowBlockFrames;

export async function* createFlowGenerator(path) {
    var flowHandle = null;
    const modelInfo = await modelIds();

    try {
        flowHandle = flowLib.FlowCreateHandle(path, FlowProperties.ref());
        var error = flowLib.FlowLastError();
        if (error) {
            console.log("Error", error, "|");
            throw new Error(error);
        }

        var nbFrames = callFlowLib(flowLib.FlowGetLength(flowHandle));
        var nbBlocks = Math.ceil(nbFrames / flowBlockFrames);
        let promiseStash = [];
        let blockPromiseCallbacks = [];
        let promisesInternal = [];

        for(var blockNum = 0; blockNum < nbBlocks; blockNum++) {
            const promise = new Promise((resolve, reject) => {
                blockPromiseCallbacks[blockNum] = {
                    resolve: resolve,
                    reject: reject,
                }
            });
            promiseStash[blockNum] = promise;
        }

        var runPromise = new Promise((resolve, reject) => {

            var buffer = Buffer.alloc(blockSize * nbBlocks);
            
            var runCallback = ffi.Callback(
                "void",
                ["pointer", "int"],
                function (flowHandle, frame_number) {
                    var blockNum = Math.floor(frame_number / flowBlockFrames) - 1;
                    var frameRange = new FrameRangeStruct({
                        fromFrame: blockNum*flowBlockFrames,
                        toFrame: Math.min((blockNum+1)*flowBlockFrames, nbFrames)
                    });

                    var subBuffer = buffer.subarray(
                        frameRange.fromFrame*blockRowSize,
                        frameRange.toFrame*blockRowSize
                    );

                    callFlowLib(flowLib.FlowGetData(flowHandle, frameRange, subBuffer));
                    if(blockPromiseCallbacks[blockNum]) {
                        blockPromiseCallbacks[blockNum].resolve({
                            data: subBuffer,
                            blockNr: blockNum+1,
                            nbBlocks: nbBlocks
                        })
                    }
                }
            );

            flowLib.FlowRun.async(flowHandle, runCallback, flowBlockFrames, function (err, res) {
                if (err) throw err;
                
                console.log("Run done (1)")
                Promise.all(promisesInternal).then(() => {
                    console.log("Run done (2)")

                    const tailFrames = nbFrames % flowBlockFrames;

                    if(tailFrames > 0) {
                        const tailFrames = nbFrames % flowBlockFrames;
                        const tailRange = new FrameRangeStruct({
                            fromFrame: nbFrames - tailFrames,
                            toFrame: nbFrames
                        });

                        let subBuffer = buffer.subarray(
                            tailRange.fromFrame*blockRowSize,
                            tailRange.toFrame*blockRowSize
                        );

                        callFlowLib(flowLib.FlowGetData(flowHandle, tailRange, subBuffer));
                        blockPromiseCallbacks[nbBlocks-1].resolve({
                            data: subBuffer,
                            blockNr: nbBlocks,
                            nbBlocks: nbBlocks
                        })

                    }

                    resolve();
                });
            });
        });

        for(var blockNum = 0; blockNum < nbBlocks; blockNum++) {
            yield promiseStash[blockNum];
        }

        await runPromise;
    } catch (e) {
        console.log("error", e);
        tmpimg = null;
    }

    if (flowHandle) {
        callFlowLib(flowLib.FlowDestroyHandle(flowHandle));
    }

}

export async function createFlow(path) {
    for await (const block of createFlowGenerator(path)) {
        await blockdb.add(block)
        console.log('Block', block);
    }
    
    return null;
}

export async function createFlowOld(path, videoId) {
    var flowHandle = null;
    var tmpimg = null;
    const modelInfo = await modelIds();

    try {
        flowHandle = flowLib.FlowCreateHandle(path, FlowProperties.ref());
        var error = flowLib.FlowLastError();
        if (error) {
            console.log("Error", error, "|");
            throw new Error(error);
        }

        var nbFrames = callFlowLib(flowLib.FlowGetLength(flowHandle));
        var nbBlocks = Math.ceil(nbFrames / flowBlockFrames);

        blockdb.add({wut:2}).then((apple) => {
            console.log("Block db (1)", apple);
        });
        
        

        var runPromise = new Promise((resolve, reject) => {

            blockdb.add({wut:2}).then((apple) => {
                console.log("Block db (2)", apple);
            });

            

            var runPromises = [];
            var buffer = Buffer.alloc(blockSize * nbBlocks);
            
            var runCallback = ffi.Callback(
                "void",
                ["pointer", "int"],
                async function (flowHandle, frame_number) {
                    var blockNum = Math.floor(frame_number / flowBlockFrames);
                    var frameRange = new FrameRangeStruct({
                        fromFrame: blockNum*flowBlockFrames,
                        toFrame: Math.min((blockNum+1)*flowBlockFrames, nbFrames)
                    });

                    blockdb.add({wut:2}).then((apple) => {
                        console.log("Block db (3)", apple);
                    });
                    
                    let myPromise = new Promise((zipResolve, zipReject) => {
                        
                        blockdb.add({wut:2}).then((apple) => {
                            console.log("Block db (4)", apple);
                        });

                        

                        var subBuffer = buffer.subarray(
                            frameRange.fromFrame*blockRowSize,
                            frameRange.toFrame*blockRowSize
                        );

                        callFlowLib(flowLib.FlowGetData(flowHandle, frameRange, subBuffer));

                        zlib.gzip(subBuffer, function (err, result) {
                            
                            blockdb.add({wut:2}).then((apple) => {
                                console.log("Block db (5)", apple);
                            });

                            if(err) {
                                zipReject(err);
                            } else {
                                zipResolve(result);
                            }
                        });
                    }).then((zipBuffer) => {
                        blockdb.add({wut:2}).then((apple) => {
                            console.log("Block db (6)", apple);
                        });

                        return ipfs.add(zipBuffer)
                    }).then((blockId) => {
                        blockdb.add({wut:2}).then((apple) => {
                            console.log("Block db (7)", apple);
                        });

                        console.log("Block added (1): ", blockId.cid.toString());

                        const blockInfo = {
                            blockId: blockId.cid.toString(),
                            videoId: videoId,
                            block: blockNum,
                            fromFrame: frameRange.fromFrame,
                            toFrame: frameRange.toFrame,
                            libId: modelInfo.libId,
                        };

                        // console.log("Block db (1)");
                        // return blockInfo.blockId
                        let result = null;
                        try {
                            result = blockdb.add(blockInfo);
                            // console.log("Block db (2)");
                            
                        } catch (e) {
                            console.log("Block add error (2)", e);
                            throw e;
                        }

                        console.log("Block db (3)", result);
                        result = result.then((dbResult) => {
                            console.log("Block db (4)", dbResult);
                        }).catch((e) => {
                            console.log("Block db (5)", e);
                        })

                        blockdb.add({wut:2}).then((apple) => {
                            console.log("Block db (8)", apple);
                        });

                        return result;
                    })

                    // myPromise = myPromise.then((entryString) => {
                    //     console.log("Flow block: " + blockNum + " / " + nbBlocks + ": " + entryString);
                    // });

                    runPromises.push(myPromise);
                }
            );

            flowLib.FlowRun.async(flowHandle, runCallback, flowBlockFrames, function (err, res) {
                if (err) throw err;
                
                console.log("Run done (1)")

                Promise.all(runPromises).then(() => {
                    console.log("Run done (2)")
                    resolve();
                });
                
            });


        });

        await runPromise;

        // callFlowLib(flowLib.FlowRun(flowHandle, runCallback, flowBlockFrames));

        // if(blockPromises.length > 0) {
        //     await Promise.all(blockPromises);
        //     console.log('All blocks added')
        // }

        // tmpimg = tempfile({'extension': 'png'});
        // callFlowLib(flowLib.FlowSave(flowHandle, tmpimg));
        // console.log('Flow saved');
    } catch (e) {
        console.log("error", e);
        tmpimg = null;
    }

    if (flowHandle) {
        callFlowLib(flowLib.FlowDestroyHandle(flowHandle));
    }

    return tmpimg;
}

export function createScript(path) {
    var flowHandle = null;
    var actions = [];

    try {
        flowHandle = flowLib.FlowCreateHandle(path, FlowProperties.ref());
        var error = flowLib.FlowLastError();
        if (error) {
            console.log("Error", error);
            throw new Error(error);
        }
        var nbFrames = callFlowLib(flowLib.FlowGetLength(flowHandle));
        var nbMs = callFlowLib(flowLib.FlowGetLengthMs(flowHandle));

        callFlowLib(
            flowLib.FlowRun(
                flowHandle,
                ffi.Callback(
                    "void",
                    ["pointer", "int"],
                    function (flowHandle, frame_number) {
                        console.log(frame_number + " / " + nbFrames);
                    }
                ),
                120
            )
        );

        var frameRange = new FrameRangeStruct({
            fromFrame: 0,
            toFrame: nbFrames,
        });

        flowLib.FlowCalcWave(
            flowHandle,
            frameRange,
            ffi.Callback(
                "void",
                [ActionStructPtr, "int", "int", "pointer"],
                function (actionsPtr, numberActions, is2, userData) {
                    const actionBuffer = actionsPtr.reinterpret(
                        numberActions * ActionStruct.size
                    );

                    for (var i = 0; i < numberActions; i++) {
                        const action = ActionStruct.get(
                            actionBuffer,
                            i * ActionStruct.size
                        );
                        const timeMs = Math.round((action.timeFrame / nbFrames) * nbMs);
                        actions.push({
                            at: timeMs,
                            pos: action.actionPos,
                        });
                    }
                }
            ),
            null
        );
    } catch (e) {
        console.log("error", e);
    }

    if (flowHandle) {
        callFlowLib(flowLib.FlowDestroyHandle(flowHandle));
    }

    return actions;
}
