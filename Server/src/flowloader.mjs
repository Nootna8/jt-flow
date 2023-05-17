import * as flowlib from './flowlib.mjs'
import {getObjectLazy, ipfs, gatewayUrl, modelIds, objectdb} from './db.mjs'
import fs from 'fs/promises'
import ffprobe from 'node-ffprobe'
import * as zlib from 'zlib'

class Job {
    constructor(video) {
        this.video = video;
    }

    async load() {
        const objectQuery = {
            _id: this.video.videoId + "-job",
            videoId: this.video.videoId
        }

        this.job = await getObjectLazy('job', objectQuery, async () => {
            return {
                status: 'queued',
                blockNr: null,
                nbBlocks: null,
                lastBlockId: null,
            }
        });
    }

    async* getBlocks() {
        yield this.job

        if(this.job.status == 'done') {    
            return
        }

        let blocks = []

        for await (const block of flowlib.createFlowGenerator(this.video.filePath)) {
            if(!block) {
                continue
            }

            if(blocks.length == 0) {
                this.job.status = 'running';
                this.job.nbBlocks = block.nbBlocks;
                await objectdb.put(this.job);
            }

            block.zip = await new Promise((resolve, reject) => {
                zlib.gzip(block.data, (err, result) => {
                    if(err)
                        reject(err)
                    else
                        resolve(result)
                })
            })
    
            const dagObj = {
                Links: [],
                Data: block.zip
            }

            if(blocks.length > 0) {
                const lastBlock = blocks[blocks.length-1];

                dagObj.Links = [{
                    Hash: lastBlock.dag.toV0(),
                    Name: "/" + block.nbBlocks + "/" + (block.blockNr-1),
                    Tsize: lastBlock.zip.length
                }]
            }
    
            block.dag = await ipfs.dag.put(dagObj, {
                storeCodec: 'dag-pb',
                hashAlg: 'sha2-256'
            })
    
            this.job.blockNr = block.blockNr;
            this.job.lastBlockId = block.dag.toV0().toString();
            if(block.nbBlocks == block.blockNr) {
                this.job.status = 'done';
            }
            await objectdb.put(this.job);
            yield this.job

            blocks.push(block);
        }
    }
}

class Video {
    constructor(inputPath) {
        this.inputPath = inputPath;
    }

    async load() {
        this.parsePath(this.inputPath)
        await this.setVideoId()
    }

    parsePath(input)
    {
        this.inputPath = input;
        this.srcPath = (typeof input === 'object') ? input.srcPath : input;

        if(typeof input === 'object')
        {
            input = input.path;
        }

        const pts = input.split('/');
        this.protocol = pts.shift();
        this.filePath = '';

        if(this.protocol == 'file') {
            // If windows add drive letter
            if(process.platform === "win32") {
                const disk = pts.shift();
                this.filePath += disk + ':/'
            }

            this.filePath += decodeURIComponent(pts.join('/'));
        }

        if(this.protocol == 'http' || this.protocol == 'https') {
            this.filePath = this.protocol + '://' + pts.join('/');
        }

        if(this.protocol == 'ipfs') {
            const hash = pts.shift();
            this.filePath = gatewayUrl + '/ipfs/' + hash;
        }

        if(!this.filePath) {
            throw new Error('Invalid path: ', inputPath);
        }

        console.log('Parsed path: ' + this.inputPath + ' -> ' + this.filePath)
    }

    async findVideoData() {
        // If local file, check if exists
        if(!this.protocol.startsWith('http')) {
            await fs.stat(this.filePath);
        }

        const info = await ffprobe(this.filePath);

        this.stream = info.streams.reduce((best, stream) => {
            if(stream.codec_type != 'video') {
                return best;
            }
    
            if(!best) {
                return stream;
            }
    
            if(stream.width > best.width || stream.height > best.height) {
                return stream;
            }
        }, null);

        if(!this.stream) {
            throw new Error('No video stream found');
        }

        return this.stream
    }

    async setVideoId() {
        const self = this

        let videoQuery = {
            srcPath: this.srcPath
        }
    
        this.videoObject = await getObjectLazy('video', videoQuery, async () => {
            const videoData = await self.findVideoData();
            const videoHash = await ipfs.add(JSON.stringify(videoData), {onlyHash: true});
            
            return {
                _id: videoHash.cid.toString(),
                inputPath: this.inputPath,
                filePath: this.filePath,
                ...videoData
            };
        });

        this.videoId = this.videoObject._id;
    }

    async getBlocks() {
        const job = new Job(this);
        await job.load();
        return job.getBlocks();
    }
}

export async function createFlow(inputPath) {
    const video = new Video(inputPath);
    await video.load();
    
    let it = await video.getBlocks();
    return it;
}