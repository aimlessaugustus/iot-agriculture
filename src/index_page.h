const char* INDEX_PAGE = R"rawliteral(
<!doctype html>
<html lang="en">
<head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <link href="https://cdn.jsdelivr.net/npm/bootstrap@5.3.2/dist/css/bootstrap.min.css" rel="stylesheet">
    <title>IoT Agriculture</title>
    
</head>
<body class="bg-light">
<div class="container py-4">
    <div class="card shadow-sm rounded mx-auto overflow-hidden" style="max-width:520px;">
        <div class="d-flex p-3 gap-3 align-items-center bg-light border-bottom">
            <div class="flex-shrink-0 bg-secondary overflow-hidden rounded" style="width:160px;height:120px;">
                <img id="cam" src="" alt="Camera (disabled)" style="width:100%;height:100%;object-fit:cover;display:block;" />
                <!--
                image src below used for Arducam Preview
                <img id="cam" src="/image?t=0" alt="Camera" style="width:100%;height:100%;object-fit:cover;display:block;" />
                -->
            </div>
            <div class="flex-grow-1">
                <h5 class="mb-1">Smart Agriculture System</h5>
                <div class="text-muted small" id="wifi">Loading…</div>
                <div class="text-muted small" id="datetime">Loading…</div>
            </div>
        </div>
        <ul class="list-group list-group-flush">
            <li class="list-group-item d-flex justify-content-between align-items-center">
                <span class="text-muted small">Temperature</span>
                <span id="temp" class="fw-semibold">Loading…</span>
            </li>
            <li class="list-group-item d-flex justify-content-between align-items-center">
                <span class="text-muted small">Humidity</span>
                <span id="hum" class="fw-semibold">Loading…</span>
            </li>
            <li class="list-group-item d-flex justify-content-between align-items-center">
                <span class="text-muted small">Water level</span>
                <span id="level" class="fw-semibold">Loading…</span>
            </li>
            <li class="list-group-item d-flex justify-content-between align-items-center">
                <span class="text-muted small">Pump</span>
                <span id="pump" class="fw-semibold">Loading…</span>
            </li>
            <li class="list-group-item d-flex justify-content-between align-items-center">
                <span class="text-muted small">Camera detection</span>
                <span id="camera" class="fw-semibold">Loading…</span>
            </li>
        </ul>
    </div>

    
</div>

<script>
// Fetch and display status/time/sensor values
async function fetchStatus(){
    try{
        const r = await fetch('/status');
        const j = await r.json();
        document.getElementById('wifi').textContent = j.connected ? ('Connected: ' + j.ip) : 'Not connected';
        if (typeof j.cameraDetected !== 'undefined') {
            document.getElementById('camera').textContent = j.cameraDetected ? 'Successful' : 'Unsuccessful';
        } else if (typeof j.camera !== 'undefined') {
            document.getElementById('camera').textContent = j.camera ? 'Successful' : 'Unsuccessful';
        }
    }catch(e){
        document.getElementById('wifi').textContent = 'Error';
    }
}
fetchStatus();

async function fetchTime(){
    try{
        const r = await fetch('/time');
        const j = await r.json();
        document.getElementById('datetime').textContent = j.datetime || 'N/A';
    }catch(e){
        document.getElementById('datetime').textContent = 'Error';
    }
}
fetchTime();
setInterval(fetchTime, 60000);

async function fetchSensor(){
    try{
        const r = await fetch('/sensor');
        const j = await r.json();
        document.getElementById('temp').textContent = j.temperature !== null ? (j.temperature + ' °C') : 'N/A';
        document.getElementById('hum').textContent = j.humidity !== null ? (j.humidity + ' %') : 'N/A';
        document.getElementById('level').textContent = j.level !== null ? (j.level + ' %') : 'N/A';
        document.getElementById('pump').textContent = j.pump !== null ? (j.pump ? 'On' : 'Off') : 'N/A';
    }catch(e){
        document.getElementById('temp').textContent = 'Error';
        document.getElementById('hum').textContent = 'Error';
        document.getElementById('pump').textContent = 'Error';
    }
    }
    fetchSensor();
    setInterval(fetchSensor, 5000);

    /*
    Arducam image fetch (disabled to stop buffers)
    async function fetchImage(){
        try{
            const timestamp = Date.now();
            document.getElementById('cam').src = '/image?t=' + timestamp;
        }catch(e){
            // ignore image fetch errors
        }
    }
    fetchImage();
    setInterval(fetchImage, 15000);
    */
</script>
</body>
</html>
)rawliteral";
