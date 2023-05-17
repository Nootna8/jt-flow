import * as IPFScore from 'ipfs-core'
import { webSockets } from '@libp2p/websockets'
import * as filters from '@libp2p/websockets/filters'
import { CID } from 'multiformats/cid'
import { multiaddr } from '@multiformats/multiaddr'
import config from './config'
import OrbitDB from 'orbit-db'


export async function* readBlocks(lastBlockId) {
    let blockHash = lastBlockId
    console.log("Getting block", lastBlockId)
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

const ipfsConfig = {
    config: {
        Bootstrap: config.Bootstrap
    },
    libp2p: {
        transports: [
            webSockets({
                filter: filters.all
            })
        ],
        // pubsub: gossipsub()
    },
}

class IpfsModule {
    constructor() {
        let me = this
        this.ipfsLoaded = new Promise(async resolve => {
            me.ipfs = await IPFScore.create(ipfsConfig)
            // await me.ipfs.swarm.connect(multiaddr(ipfsConfig.config.Bootstrap[0]))
            // await me.ipfs.swarm.connect(multiaddr(ipfsConfig.config.Bootstrap[1]))
            console.log("IPFS connected")
            resolve()
        })

        this.orbitdbLoaded = new Promise(async resolve => {
            await me.ipfsLoaded

            me.orbitdb = await OrbitDB.createInstance(me.ipfs)
            me.objectdb = await me.orbitdb.open(config.objectDBaddress, {
                type: 'docstore',
            })
            await me.objectdb.load()
            console.log("OrbitDB connected")
            resolve()
        })
    }

    async getDag(cid) {
        await this.ipfsLoaded
        return this.ipfs.dag.get(cid)
    }

    async addDbListener(type, listener) {
        await this.orbitdbLoaded

        this.objectdb.events.on('replicate.progress', (address, hash, entry, progress, have) => {
            console.log('DB update', entry)

            if(entry.payload.value.type != type) {
                return
            }

            listener(entry.payload.value)
        })
    }

    async getVideo(videoId) {
        await this.orbitdbLoaded

        let video = this.objectdb.query((o => o.type == 'video' && o._id == videoId))
        if(video.length == 0) {
            return null
        }
        console.log("Video found", videoId, video)
        
        return video[0]
    }
}

export const ipfsModule = new IpfsModule()