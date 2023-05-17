

import server from './apiapp.mjs';
import {initDB} from './db.mjs';

initDB().then(() => {
  console.log('DB initialized')

  const port = process.env.PORT || 3000;

  server.listen(port, () => {
    console.log(`JTFlow server listening on http://localhost:${port}`)
  })
})