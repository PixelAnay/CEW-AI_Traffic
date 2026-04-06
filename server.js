const http = require('http');
const fs = require('fs');
const path = require('path');

const PORT = process.env.PORT || 8080;
const LM_BASE_URL = 'http://127.0.0.1:1234';
const AI_MIN_GAP_MS = 6000;

const MIME_TYPES = {
  '.html': 'text/html; charset=utf-8',
  '.js': 'application/javascript; charset=utf-8',
  '.css': 'text/css; charset=utf-8',
  '.json': 'application/json; charset=utf-8',
  '.png': 'image/png',
  '.jpg': 'image/jpeg',
  '.jpeg': 'image/jpeg',
  '.svg': 'image/svg+xml',
  '.ico': 'image/x-icon'
};

// ── State ─────────────────────────────────────────────────────────────────────

let sensorData = { north: 10, south: 10, east: 10 };
let envData = { tempC: null, humidity: null };

let signalCommand = {
  north: 20, south: 20, east: 20,
  priority_lane: 'north',
  efficiency: 0,
  reasoning: 'Awaiting first sensor reading.'
};

let aiInFlight = false;
let lastAiRunAt = 0;

// ── Helpers ───────────────────────────────────────────────────────────────────

function setCorsHeaders(res) {
  res.setHeader('Access-Control-Allow-Origin', '*');
  res.setHeader('Access-Control-Allow-Methods', 'GET, POST, OPTIONS');
  res.setHeader('Access-Control-Allow-Headers', 'Content-Type, Authorization');
}

function getContentType(filePath) {
  return MIME_TYPES[path.extname(filePath).toLowerCase()] || 'application/octet-stream';
}

function readBody(req) {
  return new Promise((resolve, reject) => {
    const chunks = [];
    req.on('data', c => chunks.push(c));
    req.on('end', () => resolve(Buffer.concat(chunks)));
    req.on('error', reject);
  });
}

function serveFile(res, filePath) {
  if (!fs.existsSync(filePath) || !fs.statSync(filePath).isFile()) {
    res.writeHead(404, { 'Content-Type': 'application/json; charset=utf-8' });
    res.end(JSON.stringify({ error: 'Not found' }));
    return;
  }
  res.writeHead(200, { 'Content-Type': getContentType(filePath) });
  fs.createReadStream(filePath).pipe(res);
}

function sendJSON(res, status, obj) {
  res.writeHead(status, { 'Content-Type': 'application/json; charset=utf-8' });
  res.end(JSON.stringify(obj));
}

// ── LM Studio proxy ───────────────────────────────────────────────────────────

async function proxyToLmStudio(req, res) {
  try {
    const body = await readBody(req);
    const lmRes = await fetch(`${LM_BASE_URL}${req.url}`, {
      method: req.method,
      headers: { 'Content-Type': req.headers['content-type'] || 'application/json' },
      body: ['GET', 'HEAD'].includes(req.method) ? undefined : body
    });
    res.writeHead(lmRes.status, {
      'Content-Type': lmRes.headers.get('content-type') || 'application/json; charset=utf-8'
    });
    res.end(await lmRes.text());
  } catch (err) {
    res.writeHead(502, { 'Content-Type': 'application/json; charset=utf-8' });
    res.end(JSON.stringify({ error: 'LM Studio proxy error', detail: err.message }));
  }
}

// ── AI optimization ───────────────────────────────────────────────────────────

async function runAI() {
  if (aiInFlight) {
    return false;
  }

  aiInFlight = true;
  const { north, south, east } = sensorData;

  const prompt = `You are an AI traffic signal optimization engine.

Current vehicle density from ultrasonic sensors:
- North: ${Math.round(north)}%
- South: ${Math.round(south)}%
- East:  ${Math.round(east)}%

Compute optimal green signal durations. Rules:
- Total cycle: 64-96 seconds
- Higher density = longer green time
- Minimum per lane: 10 seconds
- Maximum per lane: 50 seconds
- Pick one priority lane (highest density)

Respond ONLY in valid JSON, no markdown, no extra text:
{"north":<int>,"south":<int>,"east":<int>,"efficiency":<0-100>,"priority_lane":"<north|south|east>","reasoning":"<2 sentences max>"}`;

  try {
    const response = await fetch(`${LM_BASE_URL}/v1/chat/completions`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        model: 'google/gemma-3-4b',
        max_tokens: 300,
        temperature: 0.2,
        messages: [{ role: 'user', content: prompt }]
      })
    });

    const data = await response.json();
    const raw = data.choices[0].message.content.trim().replace(/```json|```/g, '').trim();
    const result = JSON.parse(raw);

    signalCommand = result;
    console.log(`[AI] Priority: ${result.priority_lane} | Eff: ${result.efficiency}% | N:${result.north}s S:${result.south}s E:${result.east}s`);
    return true;

  } catch (err) {
    console.error('[AI ERROR]', err.message);
    return false;
  } finally {
    aiInFlight = false;
    lastAiRunAt = Date.now();
  }
}

function triggerAIIfDue() {
  const now = Date.now();
  if (aiInFlight) return;
  if (now - lastAiRunAt < AI_MIN_GAP_MS) return;
  runAI();
}

// ── Router ────────────────────────────────────────────────────────────────────

const server = http.createServer(async (req, res) => {
  setCorsHeaders(res);

  if (req.method === 'OPTIONS') {
    res.writeHead(204);
    return res.end();
  }

  const urlPath = decodeURIComponent((req.url || '/').split('?')[0]);

  // 1. LM Studio proxy — dashboard AI calls go straight through
  if (urlPath.startsWith('/v1/')) {
    return proxyToLmStudio(req, res);
  }

  // 2. NodeMCU posts real sensor readings (north + south + east)
  if (urlPath === '/sensor-data' && req.method === 'POST') {
    try {
      const body = JSON.parse((await readBody(req)).toString());
      sensorData.north = typeof body.north === 'number' ? body.north : sensorData.north;
      sensorData.south = typeof body.south === 'number' ? body.south : sensorData.south;
      sensorData.east  = typeof body.east === 'number' ? body.east : sensorData.east;
      envData.tempC = typeof body.tempC === 'number' ? body.tempC : envData.tempC;
      envData.humidity = typeof body.humidity === 'number' ? body.humidity : envData.humidity;
      console.log(`[SENSOR] N:${Math.round(sensorData.north)}% S:${Math.round(sensorData.south)}% E:${Math.round(sensorData.east)}%`);
      triggerAIIfDue();
      return sendJSON(res, 200, { ok: true });
    } catch (err) {
      return sendJSON(res, 400, { error: 'Bad JSON', detail: err.message });
    }
  }

  // 3. Dashboard reads live sensor data
  if (urlPath === '/sensor-data' && req.method === 'GET') {
    return sendJSON(res, 200, { ...sensorData, ...envData });
  }

  // 4. NodeMCU polls for latest AI signal command
  if (urlPath === '/command' && req.method === 'GET') {
    return sendJSON(res, 200, signalCommand);
  }

  // 5. Dashboard manual AI trigger
  if (urlPath === '/optimize' && req.method === 'POST') {
    await runAI();
    return sendJSON(res, 200, signalCommand);
  }

  // 6. Static file serving
  if (req.method !== 'GET') {
    return sendJSON(res, 405, { error: 'Method not allowed' });
  }

  if (urlPath === '/') {
    return serveFile(res, path.join(__dirname, 'index.html'));
  }

  const safePath = path.normalize(urlPath).replace(/^([.]{2}[/\\])+/, '');
  return serveFile(res, path.join(__dirname, safePath));
});

server.listen(PORT, () => {
  console.log(`\nSmartFlow server → http://localhost:${PORT}`);
  console.log(`  PID ${process.pid}`);
  console.log('\n  GET  /            dashboard');
  console.log('  GET  /v1/*        LM Studio proxy');
  console.log('  POST /sensor-data NodeMCU sends readings');
  console.log('  GET  /sensor-data dashboard reads live data');
  console.log('  GET  /command     NodeMCU polls signal decision');
  console.log('  POST /optimize    manual AI trigger\n');
});

server.on('error', (err) => {
  if (err && err.code === 'EADDRINUSE') {
    console.error(`\n[STARTUP ERROR] Port ${PORT} is already in use.`);
    console.error('Another SmartFlow server instance may already be running.');
    console.error('Stop the old process, or start with a different port:');
    console.error('  PowerShell: $env:PORT=8081; node server.js\n');
    process.exit(1);
  }

  console.error('\n[STARTUP ERROR]', err);
  process.exit(1);
});
