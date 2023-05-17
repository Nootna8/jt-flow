import ReconnectingWebSocket from 'reconnecting-websocket';
import config from './config';


class JTWebSocket extends EventTarget {
    constructor() {
        super();
        const me = this

        this.ws = new ReconnectingWebSocket(config.jtsocket);
        this.ws.addEventListener('message', async (event) => {
            const data = JSON.parse(event.data)
            console.log('WS message: ', data)

            me.dispatchEvent(new CustomEvent('message', {detail: data}))
        })

        this.loaded = new Promise(resolve => {
            this.ws.onopen = () => {
                console.log("WS connected")
                resolve(true)
            }
        })
    }
    
    async send(request) {
        await this.loaded;
        const me = this

        const messageId = Math.random().toString(36).slice(2, 7);
        request.messageId = messageId;
        const responsePromise = new Promise(resolve => {
            const listener = (event) => {
                const data = JSON.parse(event.data)
                if(data.messageId == messageId) {
                    me.ws.removeEventListener('message', listener)
                    resolve(data)
                }
            }
            me.ws.addEventListener('message', listener)
        })
        this.ws.send(JSON.stringify(request))

        return responsePromise
    }
    
}

export const jtWs = new JTWebSocket();