import { ipfsModule } from "./db"
import { jtWs } from "./jtsocket"
import {Base64} from 'js-base64'
import { CID } from 'multiformats/cid'
import {bpSession} from './bp.js'

class VideoTab {
    constructor(port) {
        this.port = port
        this.blocks = {}
        this.videoId = null
        this.nbBlocks = 0

        this.port.onMessage.addListener(this.onPortMessage.bind(this))
        this.port.onDisconnect.addListener(this.onDisconnect)

        console.log("Video port connected")
    }

    async readDag(blockCid) {
        var isSent = true
        var blockNr = -1
        var previousHash = null

        if(!this.blocks[blockCid]) {
            console.log("Getting block", blockCid)
            this.blocks[blockCid] = ipfsModule.getDag(blockCid)
            console.log("Got block", blockCid)

            isSent = false
        }

        const block = await this.blocks[blockCid]

        if(block.value.Links.length == 0) {
            blockNr = 1
        } else if(block.value.Links.length == 1) {

            const link = block.value.Links[0]

            const namePts = link.Name.split('/')
            if(namePts.length != 3) {
                throw new Error("Invalid block link name: ", link.Name)
            }

            blockNr = parseInt(namePts[2])+1
            const nbBlocks = namePts[1]

            if(nbBlocks != this.nbBlocks) {
                throw new Error("Invalid block nbBlocks: ", nbBlocks, this.nbBlocks)
            }

            previousHash = link.Hash
        } else {
            throw new Error("Invalid block links: ", blockNr, block)
        }
        

        if(!isSent) {
            this.port.postMessage({
                type: 'block',
                data: Base64.fromUint8Array(block.value.Data),
                blockNr,
                nbBlocks: this.nbBlocks
            })
        }

        if(previousHash) {
            this.readDag(previousHash)
        }
    }

    async onWSBlock(block) {
        if(!block.lastBlockId) {
            return
        }

        if(!this.nbBlocks) {
            this.nbBlocks = block.nbBlocks
        }

        this.readDag(CID.parse(block.lastBlockId))
    }

    async onPortMessage(msg) {
        console.log("Video port message: ", msg)
        if(msg.type == 'flow') {
            const response = await jtWs.send(msg)
            console.log("WS Response: ", response)
            
            if(response.videoId) {
                
                const videoData = await ipfsModule.getVideo(response.videoId)
                if(!videoData)
                    throw new Error("Video not found: " + response.videoId)

                console.log("Video data: ", videoData)

                this.port.postMessage({
                    type: 'init',
                    job: response,
                    video: videoData
                })

                this.videoId = response.videoId

                this.onWSBlock(response)
                
            }
            
        }
        if(msg.type == 'beat') {
            bpSession.sendPosition(msg.position, msg.duration)
        }
    }

    onDisconnect() {
        console.log("Video port disconnected")
        tabPool.remove(this.port)
    }

}

class TabPool {
    constructor() {
        this.tabs = []
        const me = this

        jtWs.addEventListener('message', (data) => {
            data = data.detail
            console.log("WS message: ", data)

            me.publishData(data)
        })

        ipfsModule.addDbListener('job', (data) => {
            console.log("Job update: ", data)

            me.publishData(data)
        })
    }

    add(port) {
        this.tabs.push(new VideoTab(port))
    }

    remove(port) {
        this.tabs = this.tabs.filter(tab => tab.port != port)
    }

    publishData(data) {
        this.tabs.forEach(tab => {
            if(!data.videoId || !tab.videoId || data.videoId != tab.videoId) {
                return
            }

            tab.onWSBlock(data)
        })
    }

}

export const tabPool = new TabPool()