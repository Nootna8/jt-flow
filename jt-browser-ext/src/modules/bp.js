import * as buttplug from "buttplug";

export class BPsession {
    constructor() {

        this.client = new buttplug.ButtplugClient("JT Browser Extension");
        this.connector = new buttplug.ButtplugBrowserWebsocketClientConnector("ws://localhost:12345/");
        
        this.devices = []
        
        this.client.addListener('deviceadded', (device) => {
            console.log("Device added: ", device)
            this.devices.push(device)
        })
        this.client.addListener('deviceremoved', (device) => {
            console.log("Device removed: ", device)
            this.devices = this.devices.filter(d => d != device)
        })

    }

    connect() {
        this.client.connect(this.connector).then(() => {
            console.log("Connected to Buttplug")
            this.client.startScanning()
        })
    }

    sendPosition(position, duration) {
        
        this.devices.forEach(device => {
            if(!device.messageAttributes.LinearCmd) {
                return
            }

            device.linear(position, parseInt(duration * 1000))
        })

    }
}

export const bpSession = new BPsession()

export class BPplayer {
    constructor(videoElement, port) {
        this.beats = []
        this.port = port
        this.videoElement = videoElement
        videoElement.addEventListener('timeupdate', () => this.videoPositionUpdate())
        this.lastPosition = 0
        this.lastPositionAt = 0
        this.lastType = 0
        this.flip = false
        
        this.interval = setInterval(() => this.update(), 100)
    }

    async update() {
        if(this.beats.length == 0) {
            return
        }

        const now = Date.now()
        const elapsed = now - this.lastPositionAt
        if(elapsed > 1000) {
            return;
        }

        const position = this.lastPosition + elapsed / 1000

        const beat = this.beats.find(b => b.pos > position)
        if(!beat || beat.type == this.lastType) {
            return
        }

        this.lastType = beat.type

        const nextBeat = this.beats.find(b => b.pos > beat.pos)
        if(!nextBeat) {
            return
        }

        let beatPosition = 0.1
        if(beat.type == 1) {
            beatPosition = 0.9
        }

        if(this.flip) {
            beatPosition = 1 - beatPosition
        }

        const duration = nextBeat.pos - beat.pos
        this.port.postMessage({
            type: 'beat',
            position: beatPosition,
            duration: duration,
        })
    }

    setBeats(beats) {
        this.beats = beats
    }

    videoPositionUpdate() {
        this.lastPosition = this.videoElement.currentTime
        this.lastPositionAt = Date.now()
    }
}