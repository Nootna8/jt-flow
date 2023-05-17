import {tabPool} from './modules/tabs.js'
import {bpSession} from './modules/bp.js'

bpSession.connect()

chrome.runtime.onConnect.addListener(async function(port) {
    if(port.name == 'video') {
        tabPool.add(port)
        return
    }
});


async function handleMessage(type, message, sendResponse) {
    if(type == 'flow') {
        const response = await websocketRequest(message)
        sendResponse(response)
        return
    }

    throw new Error("Unknown message type: " + type)
}

chrome.runtime.onMessage.addListener(
    function(request, sender, sendResponse) {
      console.log(sender.tab ?
                  "from a content script:" + sender.tab.url :
                  "from the extension");

        try {
            handleMessage(request.type, request, sendResponse)
            return true
        } catch(e) {
            console.error(e)
            sendResponse({error: e})
        }
    }
);