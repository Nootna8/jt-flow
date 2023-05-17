import * as IPFS from 'ipfs-core'
import OrbitDB from 'orbit-db'
import {HttpGateway} from 'ipfs-http-gateway'
import fs from 'fs/promises'
import path from 'path'
import * as flowlib from './flowlib.mjs'
import {ipfsConfig} from './ipfs.mjs'
import { multiaddr } from '@multiformats/multiaddr'

export let ipfs = null;
export let orbitdb = null;
export let objectdb = null;
export let blockdb = null;
export let objectdbprivate = null;


let gateway = null;
export let gatewayUrl = null;

let dbVersion = 'v4'

export async function initDB() {
    try {
        const repoLoclStat = await fs.stat('./ipfs/repo.lock')
        await fs.rmdir('./ipfs/repo.lock')
        console.log('Cleared repo lock')
    } catch(e) {}

    ipfs = await IPFS.create(ipfsConfig)
    // await ipfs.swarm.connect(multiaddr('/ip4/127.0.0.1/tcp/4008/ws/p2p/12D3KooWR9qeGJWaLZj94AyhWgxyKDeyvKmdRSU1eNR4nJ1xGzWR'))
    gateway = new HttpGateway(ipfs)
    await gateway.start()
    gatewayUrl = gateway._gatewayServers[0].info.uri
    console.log('IPFS Gateway: ', gatewayUrl)

    orbitdb = await OrbitDB.createInstance(ipfs, {directory: './ipfs/orbitdb'})

    objectdb = await orbitdb.docs('jtflow-objects')
    await objectdb.load()
    console.log('Object Db: ', objectdb.address.toString())

    blockdb = await orbitdb.eventlog('jtflow-eventlog')
    await blockdb.load()
    console.log('Block Db: ', blockdb.address.toString())
}

export async function getObjectLazy(type, objectQuery, loader) {
    objectQuery.dbVersion = dbVersion;
    objectQuery.type = type;

    var objectQueryFunc = (o => {
        for(var key in objectQuery) {
            if(o[key] != objectQuery[key]) {
                return false;
            }
        }
        return true;
    });

    let result = objectdb.query(objectQueryFunc);
    if(result.length == 1) {
        return result[0];
    } else if(result.length > 1) {
        throw new Error('Multiple objects found');
    } else {
        result = await loader();
        if(!result) {
            throw new Error('Object loader failed');
        }
        result = {
            createdAt: new Date().toISOString(),
            ...result,
            ...objectQuery
        }
        await objectdb.put(result);
        console.log('Object added: ', result)
        return result;
    }
}

export async function modelIds() {
    const modelFile = await fs.readFile('jtmodel.py');
    const modelResult = await ipfs.add(modelFile, {onlyHash: true});
    const modelQuery = {'_id': modelResult.cid.toString()};

    const modelInfo = await getObjectLazy('model', modelQuery, async () => {
        const baseName = path.basename('jtmodel.py');
        const stats = await fs.stat('jtmodel.py');
        
        return {
            mtime: stats.mtime.toISOString(),
            path: baseName
        }
    });

    const libFile = await fs.readFile(flowlib.libFile);
    const libResult = await ipfs.add(libFile, {onlyHash: true});
    const libQuery = {'_id': libResult.cid.toString()};

    const libInfo = await getObjectLazy('lib', libQuery, async () => {
        const baseName = path.basename(flowlib.libFile);
        const stats = await fs.stat(flowlib.libFile);
        
        return {
            mtime: stats.mtime.toISOString(),
            path: baseName
        }
    });

    return {
        modelId: modelInfo._id,
        libId: libInfo._id
    }
}