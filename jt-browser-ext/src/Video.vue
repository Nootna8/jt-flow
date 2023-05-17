<template>
    <div class="wrapper">
        JT Status: {{ status }} <br/>
        <button v-if="player" @click="player.flip = !player.flip">Flip {{ player.flip ? 'yes':'no' }}</button> <br/>

        <div :class="'canvasWrapper' + ' canvasWrapperHook'+hookType">
            <canvas ref="canvas"></canvas>
        </div>
    </div>
</template>

<script>
    import cv from './modules/opencv11'
    import {unzipBase64} from './modules/utils'
    import * as _ from 'lodash'
    import * as flowmodel from './modules/flowmodel'
    import {BPplayer} from './modules/bp'

    let port = null;
    let mat = null;
    let matTmp = null;

    export default {
        name: 'Video',
        watch: {
            zoom() {
                this.updateCanvasDebounce();
            }
        },
        methods: {
            updateScores() {
                if(!this.scores)
                    return

                this.beats = flowmodel.stage5(this.scores);

                
                if(!this.videoElm.duration || !this.nbFrames) {
                    return
                }
                
                const videoDuration = this.videoElm.duration;
                const me = this

                const beatsMs = this.beats.map(b => {
                    return {
                        pos: (b.pos / me.nbFrames) * videoDuration,
                        val: b.val,
                        type: b.type
                    }
                })

                console.log("Vid duration", videoDuration, "beats", beatsMs)

                if(!this.player) {
                    this.player = new BPplayer(this.videoElm, port);
                }

                this.player.setBeats(beatsMs);
            },

            async onPortMessage(msg) {
                console.log("Port message" , msg);
                this.status = 'Connected';

                if(msg.type == 'init') {
                    this.nbFrames = parseInt(msg.video.nb_frames);
                    console.log(this.nbFrames + ' frames')
                    return
                }

                if(msg.type != 'block')
                    return

                if(mat == null) {
                    mat = new cv.Mat(msg.nbBlocks * 400, 180, cv.CV_32S);
                    mat.setTo([0, 0, 0, 0])
                }

                let data = msg.data;
                data = await unzipBase64(data);

                const blockData = new Int32Array(data.buffer)
                let blockMat = cv.matFromArray(400, 180, cv.CV_32S, blockData)
                let copyFrom = (msg.blockNr-1) * 400;
                let copyTo = (msg.blockNr) * 400;

                let matRange = mat.rowRange(copyFrom, copyTo);
                

                blockMat.copyTo(matRange)
                blockMat.delete();

                this.updateCanvasDebounce('all');
                
            },
            updateCanvas(stage) {
                if(!mat) {
                    return
                }

                if(!matTmp) {
                    matTmp = new cv.Mat(90, mat.rows, cv.CV_8UC3);
                    stage = 'all'
                }

                if(stage == 'all') {
                    flowmodel.visualize(mat, matTmp);
                    this.scores = flowmodel.process(mat);
                    this.updateScores();
                }

                let matTmp2 = new cv.Mat();
                matTmp.copyTo(matTmp2);

                const playerTimeFrame = this.videoElm.currentTime / this.videoElm.duration * matTmp.cols;
                cv.line(matTmp2, new cv.Point(playerTimeFrame, 0), new cv.Point(playerTimeFrame, matTmp.rows), [255, 0, 255, 255], 2, cv.LINE_8, 0)
                
                // if(this.zoom != 1) {
                //     cv.resize(matTmp2, matTmp2, new cv.Size(matTmp.cols * this.zoom, matTmp.rows * this.zoom), 0, 0, cv.INTER_NEAREST);
                // }

                this.plotScores(matTmp2);
                
                cv.imshow(this.$refs.canvas, matTmp2);
                
            },
            plotScores(matTmp2) {
                if(!this.scores) {
                    return
                }

                // for(let x=1; x<mat.rows; x++) {
                //     const score = Math.min(180, Math.max(0, (this.scores[x] / 60) + 90));
                //     const lastScore = Math.min(180, Math.max(0, (this.scores[x-1] / 60) + 90));

                //     cv.line(matTmp2, new cv.Point((x-1)*2, lastScore), new cv.Point(x*2, score), [255, 0, 255, 255], 1, cv.LINE_8, 0)
                // }

                for(let b=1; b<this.beats.length; b++) {
                    let positionFrames = this.beats[b].pos
                    let lastPositionFrames = this.beats[b-1].pos

                    const beat = this.beats[b];
                    const lastBeat = this.beats[b-1];

                    const score = Math.min(180, Math.max(0, (beat.val / 40) + 45));
                    const lastScore = Math.min(180, Math.max(0, (lastBeat.val / 40) + 45));

                    let color = [0, 0, 255, 255];
                    if(beat.type == 1) {
                        color = [255, 0, 0, 255];
                    }
                    cv.circle(matTmp2, new cv.Point(positionFrames, score), 3, color, 3, cv.LINE_8, 0)
                    if(b > 0) {
                        color = [255, 255, 255, 255]
                        cv.line(matTmp2, new cv.Point(lastPositionFrames, lastScore), new cv.Point(positionFrames, score), color, 1, cv.LINE_8, 0)
                    }
                }
            }
        },
        data() {
            return {
                status: 'Loading',
                zoom: 1,
                videoElm: null,
                scores: null,
                hookType: null,
                player: null,
                nbFrames: null,
                beats: []
            }
        },
        async created() {
            console.log("Init video", window.hostType)

            await cv.ready

            this.updateCanvasDebounce = _.throttle(this.updateCanvas, 3000);

            let src = null;
            this.hookType = window.hostType;

            if(window.hostType == 'spang') {
                this.videoElm = document.querySelector("video.vjs-tech");
                if(!this.videoElm) {
                    this.status = 'No video found';
                    return;
                }
                this.videoElm.addEventListener('timeupdate', this.updateCanvasDebounce.bind(this));
                this.videoElm.addEventListener('durationchange', this.updateScores.bind(this));
                src = this.videoElm.src.replace('://', '/')
            }

            if(window.hostType == 'ph') {
                try {
                    let hook_promise = new Promise(resolve => {
                        // Event listener
                        document.addEventListener('jt_ph_rook_result', function(e) {
                            resolve(e.detail)
                        });
                    })

                    var s = document.createElement('script');
                    s.src = chrome.runtime.getURL('src/ph_hook.js');
                    // see also "Dynamic values in the injected code" section in this answer
                    (document.head || document.documentElement).appendChild(s);

                    const hookResult = await hook_promise;
                    let srcUrl = hookResult.mediaDefinitions.find(m => m.format == 'mp4').videoUrl;
                    srcUrl = await fetch(srcUrl).then(r => r.json());
                    srcUrl = srcUrl.reduce((a, b) => parseInt(a.quality) > parseInt(b.quality) ? a : b).videoUrl;

                    this.videoElm = document.querySelector('.mgp_videoWrapper video');
                    this.videoElm.addEventListener('timeupdate', this.updateCanvasDebounce.bind(this));
                    this.videoElm.addEventListener('durationchange', this.updateScores.bind(this));
                    src = srcUrl.replace('://', '/')
                } catch(e) {
                    console.log("No video found", e)
                    this.status = 'No video found';
                    return;
                }
            }
            
            
            this.status = 'Loading .';

            port = chrome.runtime.connect({name: 'video'});
            port.onMessage.addListener(this.onPortMessage);
            port.postMessage({
                type: "flow",
                path: {
                    path: src,
                    srcPath: window.location.href
                }
            });

            console.log("Init video", {
                type: "flow",
                path: {
                    path: src,
                    srcPath: window.location.href
                }
            })

            this.status = 'Loading ..';
        }
    }
</script>

<style scoped>
    .wrapper {
        width: 100%;
        margin-top: 50px;
        margin-bottom: 50px;
    }

    .canvasWrapper {
        overflow-x: scroll;
    }

    .canvasWrapper.canvasWrapperHookspang::-webkit-scrollbar {
        /* height: 20px; */
    }
</style>