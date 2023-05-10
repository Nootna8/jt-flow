import express from 'express';
import * as ffi from 'ffi-napi'
import * as ref from 'ref-napi'
import * as StructDi from 'ref-struct-di'
import tempfile from 'tempfile';
import fs from 'fs';
import path from 'path';
import * as IPFS from 'ipfs-core'
import OrbitDB from 'orbit-db'
import { env } from 'process';

const StructType = StructDi.default (ref);

var FrameNumberType = ref.types.ulong;

const FlowPropertiesStruct = StructType({
  'numberOfPools': ref.types.int,
  'maxValue': ref.types.float,
  'overlayHalf': ref.types.bool,
  'focusPoint': ref.types.float,
  'focusSize': ref.types.float,
  'waveSmoothing1': ref.types.float
});

var FrameRangeStruct = StructType({
  'fromFrame': FrameNumberType,
  'toFrame': FrameNumberType
});

const ActionStruct = StructType({
  'timeFrame': ref.types.int,
  'actionPos': ref.types.int
});
const ActionStructPtr = ref.refType(ActionStruct);

var FlowProperties = new FlowPropertiesStruct({
  'numberOfPools': 180,
  'maxValue': 0.2,
  'overlayHalf': false,
  'focusPoint': 0.5,
  'focusSize': 0.5,
  'waveSmoothing1': 0.5
});

var FlowPropertiesPtr = ref.refType(FlowPropertiesStruct);

var lib = env.FLOWLIB || '/app/FlowLib/build/libJTFlowLav'
// var lib = env.FLOWLIB  || 'C:/dev/JackerTracker/JTFlow/FlowLib/build/Release/JTFlowCuda.dll'

try {
  var flowLib = ffi.Library(lib, {
    'FlowCreateHandle': [ 'pointer', [ 'string', FlowPropertiesPtr ] ],
    'FlowDestroyHandle': [ 'bool', [ 'pointer' ] ],
    'FlowRun': ['bool', ['pointer', 'pointer', 'int']],
    'FlowGetLength': [FrameNumberType, ['pointer']],
    'FlowGetLengthMs': [FrameNumberType, ['pointer']],
    'FlowSave': ['bool', ['pointer', 'string']],
    'FlowCalcWave': ['bool', ['pointer', FrameRangeStruct, 'pointer', 'pointer']],
    'FlowLastError': ['string', []],
    'FlowSetLogger': ['bool', ['pointer']]
  });
} catch(e) {
  console.log('Library error', e);
  process.exit(1);
}

function callFlowLib(result) {
  if(!result) {
    var error = flowLib.FlowLastError();
    console.log('Error falsy result: ', error, '|');
    throw new Error(error);
  }

  return result;
}

var logCallback = ffi.Callback('void', ['int', 'string'], function(level, message) {
  console.log('FlowLib: ', message);
})

// callFlowLib(flowLib.FlowSetLogger(logCallback));


function createFlow(path) {
  var flowHandle = null;
  var tmpimg = null;

  try {
    flowHandle = flowLib.FlowCreateHandle(path, FlowProperties.ref());
    var error = flowLib.FlowLastError();
    if(error) {
      console.log('Error', error, '|');
      throw new Error(error);
    }
    var nbFrames = callFlowLib(flowLib.FlowGetLength(flowHandle));

    callFlowLib(flowLib.FlowRun(flowHandle, ffi.Callback('void', ['pointer', 'int'], function(flowHandle, frame_number) {
      console.log(frame_number + " / " + nbFrames);
    }), 120));

    var frameRange = new FrameRangeStruct({
      'fromFrame': 0,
      'toFrame': nbFrames
    });

    tmpimg = tempfile({'extension': 'png'});
    callFlowLib(flowLib.FlowSave(flowHandle, tmpimg));
    console.log('Flow saved');

  } catch(e) {
    console.log('error', e);
    tmpimg = null;
  }

  if(flowHandle) {
    callFlowLib(flowLib.FlowDestroyHandle(flowHandle));
  }

  return tmpimg;
}

function createScript(path) {
  var flowHandle = null;
  var actions = [];

  try {
    flowHandle = flowLib.FlowCreateHandle(path, FlowProperties.ref());
    var error = flowLib.FlowLastError();
    if(error) {
      console.log('Error', error);
      throw new Error(error);
    }
    var nbFrames = callFlowLib(flowLib.FlowGetLength(flowHandle));
    var nbMs = callFlowLib(flowLib.FlowGetLengthMs(flowHandle));

    callFlowLib(flowLib.FlowRun(flowHandle, ffi.Callback('void', ['pointer', 'int'], function(flowHandle, frame_number) {
      console.log(frame_number + " / " + nbFrames);
    }), 120));

    var frameRange = new FrameRangeStruct({
      'fromFrame': 0,
      'toFrame': nbFrames
    });

    flowLib.FlowCalcWave(flowHandle, frameRange, ffi.Callback('void', [ActionStructPtr, 'int', 'int', 'pointer'], function(actionsPtr, numberActions, is2, userData) {
      const actionBuffer = actionsPtr.reinterpret(numberActions*ActionStruct.size);
      
      for(var i = 0; i < numberActions; i++) {
        const action = ActionStruct.get(actionBuffer, i*ActionStruct.size)
        const timeMs = Math.round(action.timeFrame / nbFrames * nbMs);
        actions.push({
          at: timeMs,
          pos: action.actionPos
        });
      }

    }), null);

  } catch(e) {
    console.log('error', e);
  }

  if(flowHandle) {
    callFlowLib(flowLib.FlowDestroyHandle(flowHandle));
  }

  return actions;
}

async function modelHash(ipfs) {
  const model = await fs.readFile('jtmodel.py');
  const result = await ipfs.add(model);
  return result.cid.toString();
}

function runApp(ipfs, db) {
  const port = env.PORT || 3000;
  const app = express();

  app.get('/', (req, res) => {
    res.send('Hello World!')
  })

  app.get('/flow/*', (req, res) => {
    var inputPath = req.originalUrl.substring(6);
    var pts = inputPath.split('/');

    console.log(pts);
    
    if(pts.length < 3) {
      res.sendStatus(404);
      console.log('Too short');
      return;
    }

    var protocol = pts.shift();

    if(protocol == 'file') {
      var disk = pts.shift();

      if(process.platform === "win32") {
        inputPath = disk + ':/' + decodeURIComponent(pts.join('/'));
      } else {
        inputPath = '/' + disk + '/' + decodeURIComponent(pts.join('/'));
      }

      if(!fs.existsSync(inputPath)) {
        res.sendStatus(404);
        console.log(inputPath + ' does not exist');
        return;
      }
    }
    else if(protocol == 'http' || protocol == 'https') {
      inputPath = protocol + '://' + pts.join('/');
      //Todo, check url
    } else {
      res.sendStatus(404);
      console.log(protocol + ' is not supported');
      return;
    }

    const flow = createFlow(inputPath);

    // var fileName = inputPath.parse(path).name + '.png';
    // res.setHeader('Content-Disposition', 'attachment; filename=' + fileName);
    res.sendFile(flow);

    // (async function() {
    //   var hash = await db.get(path);

    //   if(!hash) {
    //     const flow = createFlow(path);
    //     if(!flow) {
    //       res.sendStatus(500);
    //       return;
    //     }

    //     const buffer = fs.readFileSync(flow)
    //     const result = await ipfs.add(buffer)
    //     hash = result.cid.toString();
    //     db.set(path, hash);
    //   }

    //   res.setHeader('Content-Type', 'image/png');
    //   res.setHeader('Transfer-Encoding', 'chunked');
      
    //   for await (const chunk of ipfs.cat(hash)) {
    //     res.write(chunk);
    //   }
    //   res.end();

    // })();
  })

  app.get('/script/*', (req, res) => {
    var inputPath = req.originalUrl.substring(8);
    var pts = inputPath.split('/');

    console.log(pts);
    
    if(pts.length < 3) {
      res.sendStatus(404);
      console.log('Too short');
      return;
    }

    var protocol = pts.shift();

    if(protocol == 'file') {
      var inputPath = null;
      var disk = pts.shift();

      if(process.platform === "win32") {
        inputPath = disk + ':/' + decodeURIComponent(pts.join('/'));
      } else {
        inputPath = '/' + disk + '/' + decodeURIComponent(pts.join('/'));
      }

      if(!fs.existsSync(inputPath)) {
        res.sendStatus(404);
        console.log(inputPath + ' does not exist');
        return;
      }
    }
    else if(protocol == 'http' || protocol == 'https') {
      inputPath = protocol + '://' + pts.join('/');
      //Todo, check url
    } else {
      res.sendStatus(404);
      console.log(protocol + ' is not supported');
      return;
    }

    const actions = createScript(inputPath);
    
    var fileExt = path.extname(inputPath);
    var fileName = path.basename(inputPath, fileExt) + '.funscript';
    res.setHeader('Content-Disposition', 'attachment; filename=' + fileName);

    res.json({actions})

    // (async function() {
    //   var hash = await db.get(path);

    //   if(!hash) {
    //     const flow = createFlow(path);
    //     if(!flow) {
    //       res.sendStatus(500);
    //       return;
    //     }

    //     const buffer = fs.readFileSync(flow)
    //     const result = await ipfs.add(buffer)
    //     hash = result.cid.toString();
    //     db.set(path, hash);
    //   }

    //   res.setHeader('Content-Type', 'image/png');
    //   res.setHeader('Transfer-Encoding', 'chunked');
      
    //   for await (const chunk of ipfs.cat(hash)) {
    //     res.write(chunk);
    //   }
    //   res.end();

    // })();
  })

  app.listen(port, () => {
    console.log(`Example app listening on port ${port}`)
  })
}

(async function() {
  const ipfs = await IPFS.create({ repo : './ipfs' })
  const orbitdb = await OrbitDB.createInstance(ipfs, {directory: './ipfs/orbitdb'})

  // Create / Open a database
  const db = await orbitdb.keyvalue('flowcache')
  await db.load()
  console.log(db.address.toString());

  runApp(ipfs, db);
})();