import * as flowlib from './flowlib.mjs';
import {initDB, ipfs, blockdb, objectdb} from './db.mjs';
import { CID } from 'multiformats/cid'
import { sha256 } from 'multiformats/hashes/sha2'
import * as dagPB from '@ipld/dag-pb'
import { fromString as uint8ArrayFromString } from 'uint8arrays/from-string'
import * as zlib from "zlib";

// const parsed = CID.parse('bafybeibyc73pdv76ycowjbdijjglqfxksrayhj6xl7il2ckeyemrgveppi');
// console.log('CID', parsed.toV0().toString())

const video = 'C:/Users/Electromuis/Downloads/tmp/OilReversCowgirl.mp4'

async function* readBlocks(lastBlockId) {
    let blockHash = lastBlockId
    let lastBlock = await ipfs.dag.get(blockHash)
    
    if(lastBlock.value.Links.length != 1) {
        return lastBlock
    }
    var nbBlocks = parseInt(lastBlock.value.Links[0].Name.substring(1))
    if(!nbBlocks) {
        return
    }
    nbBlocks += 2

    console.log("Promising " + nbBlocks + " blocks");

    let promiseStash = []
    let resolvers = []
    for(var b=0; b<nbBlocks; b++) {
        promiseStash[b] = new Promise((resolve) => {
            resolvers[b] = resolve
        })
    }

    (async () => {
        let block = lastBlock;
        var blockNr = nbBlocks-1
        // console.log('Resolving ' + (blockNr))
        // resolvers[blockNr](block);

        while(block.value.Links.length == 1) {
            var previousblockNr = parseInt(block.value.Links[0].Name.substring(1))
            if(!previousblockNr && previousblockNr !== 0) {
                throw new Error("Invalid block nr: " + block.value.Links[0].Name)
            }

            if(previousblockNr != (blockNr-1)) {
                throw new Error("Block chain broken, " + blockNr + " - " + previousblockNr)
            }

            resolvers[blockNr]({block, blockNr, Hash: blockHash});
            
            blockHash = block.value.Links[0].Hash
            block = await ipfs.dag.get(blockHash)

            blockNr = previousblockNr
        }

        if(blockNr !== 0) {
            throw new Error("Block chain not ended, " + blockNr)
        }

        resolvers[blockNr]({block, blockNr, Hash: blockHash});
    })();
    
    for(var b=0; b<nbBlocks; b++) {
        yield promiseStash[b];
    }

    await Promise.all(promiseStash)
}


async function main() {
    await initDB()
    console.log('DB initialized')
    let lastBlock = null;

    for await (const block of flowlib.createFlowGenerator(video)) {
        // await blockdb.add(block)
        if(!block) {
            console.log('Empty block', block);
            continue
        } else {
            console.log('Got block', block.blockNr);
        }

        const blockZip = await new Promise((resolve, reject) => {
            zlib.gzip(block.data, (err, result) => {
                if(err)
                    reject(err)
                else
                    resolve(result)
            })
        })

        const dagObj = {
            Links: [],
            Data: blockZip
        }

        if(lastBlock) {
            dagObj.Links = [lastBlock]
        }

        const dag = await ipfs.dag.put(dagObj, {
            storeCodec: 'dag-pb',
            hashAlg: 'sha2-256'
        })

        // console.log('Dag1', dag.toV0())
        
        const data = await ipfs.dag.get(dag)
        // console.log('Data', data)

        lastBlock = {
            Hash: dag,
            Name: "/" + (block.blockNr),
            Tsize: block.length
        };
    }

    for await (const block of readBlocks(lastBlock.Hash)) {
        console.log('Block', block)
        // console.log(block.value.Links[0])
    }
}

main().then(() => {
    console.log('Done')
    system.exit(0)
})