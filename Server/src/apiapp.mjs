import express from 'express';
import { WebSocketServer } from 'ws'
import * as http from 'http';

import {createFlow} from './flowloader.mjs';
import {ipfs} from './db.mjs';

import path from 'path'
import fs from 'fs/promises'

const app = express();

async function* walk(dir) {
    for await (const d of await fs.opendir(dir)) {
        const entry = path.join(dir, d.name);
        if (d.isDirectory()) yield* walk(entry);
        else if (d.isFile()) yield entry;
    }
}

app.use((err, req, res, next) => {
    console.error(err.stack)
    res.status(500).send('Something broke!')
})

app.get('/', (req, res) => {
    res.send('Hello World!')
})

app.get('/flow/(.*)', async (req, res) => {
    var inputPath = req.params['0']
    console.log("Flow request")
    res.setHeader('Content-Type', 'application/json');
    res.write('[')
    let job = null;
    const it = await createFlow(inputPath);

    for await (const jobStatus of it) {
        if(job) {
            res.write(",")
        }
        job = jobStatus;
        // console.log("Job status: ", jobStatus)
        res.write(JSON.stringify(jobStatus) + '\n')
    }

    res.write(']')
    res.end();
})


app.get('/old/flow/(.*)', async (req, res) => {
    var inputPath = req.params['0']
    console.log("Flow request")
    const flow = await createFlow(inputPath);
    console.log('Flow : ', flow);

    // res.redirect(gatewayUrl + '/ipfs/' + flow._id);

    res.setHeader('Content-Type', 'image/png');
    res.setHeader('Transfer-Encoding', 'chunked');

    for await (const chunk of ipfs.cat(flow._id)) {
        res.write(chunk);
    }
    res.end();
})

app.get('/old/script/.*', (req, res) => {
    console.log("Script request")

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
})

const server = http.createServer(app);
const wss = new WebSocketServer({ server });

export default server;

async function wsFlow(ws, data) {
    let messageId = data.messageId;
    const it = await createFlow(data.path);

    for await (const jobStatus of it) {
        ws.send(JSON.stringify({
            messageId,
            ...jobStatus
        }));
    }
}

function handleWsMessage(ws, type, data) {
    let messageId = data.messageId;

    if(type == 'handshake') {
        ws.send(JSON.stringify({type: 'handshake', messageId}));
        return true;
    }

    if(type == 'flow') {
        
        wsFlow(ws, data).catch(e => {
            console.error(e);
            ws.send(JSON.stringify({
                messageId,
                type: 'error',
                message: e.message
            }))
        });

        return true;
    }

    throw new Error('Unknown message type: ' + type);
}

wss.on('connection', (ws) => {
    ws.on('message', (message) => {
        try {
            message = JSON.parse(message);

            console.log('received: %s', message);
            handleWsMessage(ws, message.type, message);
        } catch (e) {
            console.error(e);
            ws.send(JSON.stringify({
                type: 'error',
                message: e.message
            }))
        }
    });

    //send immediatly a feedback to the incoming connection    
    ws.send(JSON.stringify({
        type: 'handshake'
    }));
});