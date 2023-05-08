import express from 'express';
import * as ffi from 'ffi-napi'
import * as ref from 'ref-napi'
import * as StructDi from 'ref-struct-di'
import tempfile from 'tempfile';
import fs from 'fs';
import * as IPFS from 'ipfs-core'
import OrbitDB from 'orbit-db'
import * as stream from 'stream'
import { env } from 'process';

const StructType = StructDi.default (ref);

var FrameNumberType = ref.types.ulong;

var FlowPropertiesStruct = StructType({
  'numberOfPools': ref.types.int,
  'maxValue': ref.types.float,
  'overlayHalf': ref.types.bool,
  'focusPoint': ref.types.float,
  'focusSize': ref.types.float,
  'waveSmoothing1': ref.types.float
});

var FlowProperties = new FlowPropertiesStruct({
  'numberOfPools': 180,
  'maxValue': 0.2,
  'overlayHalf': false,
  'focusPoint': 0.5,
  'focusSize': 0.5,
  'waveSmoothing1': 0.5
});

var FlowPropertiesPtr = ref.refType(FlowPropertiesStruct);

var flowLib = ffi.Library('/usr/local/src/FlowLib/build/libJTFlowLav', {
  'FlowCreateHandle': [ 'pointer', [ 'string', FlowPropertiesPtr ] ],
  'FlowDestroyHandle': [ 'void', [ 'pointer' ] ],
  'FlowRun': ['void', ['pointer', 'pointer']],
  'FlowGetLength': [FrameNumberType, ['pointer']],
  'FlowSave': ['void', ['pointer', 'string']]
});

function createFlow(path) {
  var flowHandle = null;
  var tmpimg = null;

  try {
    flowHandle = flowLib.FlowCreateHandle(path, FlowProperties.ref());
    var nbFrames = flowLib.FlowGetLength(flowHandle);

    flowLib.FlowRun(flowHandle, ffi.Callback('void', ['pointer', 'int'], function(flowHandle, frame_number) {
      if(frame_number > 0 && frame_number % 120 == 0) {
        console.log(frame_number + " / " + nbFrames);
      }
    }));

    tmpimg = tempfile({'extension': 'png'});
    flowLib.FlowSave(flowHandle, tmpimg);
  } catch(e) {
    console.log('error', e);
    tmpimg = null;
  }

  if(flowHandle) {
    flowLib.FlowDestroyHandle(flowHandle);
  }

  return tmpimg;
}

function runApp(ipfs, db) {
  const port = env.PORT || 3000;
  const app = express();

  app.get('/', (req, res) => {
    res.send('Hello World!')
  })

  app.get('/flow/*', (req, res) => {
    var path = req.originalUrl.substring(6);
    var pts = path.split('/');

    console.log(pts);
    
    if(pts.length < 3) {
      res.sendStatus(404);
      console.log('Too short');
      return;
    }

    var protocol = pts.shift();

    if(protocol == 'file') {
      var disk = pts.shift();
      // var path = disk + ':/' + decodeURIComponent(pts.join('/'));
      var path = '/' + disk + '/' + decodeURIComponent(pts.join('/'));
      if(!fs.existsSync(path)) {
        res.sendStatus(404);
        console.log(path + ' does not exist');
        return;
      }
    }
    else if(protocol == 'http' || protocol == 'https') {
      path = protocol + '://' + pts.join('/');
      //Todo, check url
    } else {
      res.sendStatus(404);
      console.log(protocol + ' is not supported');
      return;
    }

    (async function() {
      var hash = await db.get(path);

      if(!hash) {
        const flow = createFlow(path);
        if(!flow) {
          res.sendStatus(500);
          return;
        }


        const buffer = fs.readFileSync(flow)
        const result = await ipfs.add(buffer)
        hash = result.cid.toString();
        db.set(path, hash);
      }

      res.setHeader('Content-Type', 'image/png');
      res.setHeader('Transfer-Encoding', 'chunked');

      
      for await (const chunk of ipfs.cat(hash)) {
        res.write(chunk);
      }
      res.end();

    })();

  })

  app.listen(port, () => {
    console.log(`Example app listening on port ${port}`)
  })
}



(async function() {
  const ipfs = await IPFS.create({ repo : './ipfs3' })
  const orbitdb = await OrbitDB.createInstance(ipfs)

  // Create / Open a database
  const db = await orbitdb.keyvalue('flowcache')
  await db.load()
  console.log(db.address.toString());

  runApp(ipfs, db);
})();