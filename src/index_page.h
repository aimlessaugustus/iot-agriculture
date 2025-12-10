// Mobile-friendly Bootstrap landing page
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
    <div class="text-center">
        <h1 class="display-6">IoT Agriculture Device</h1>
        <p class="lead">Arduino R4 WiFi</p>
        <div class="row gy-3">
            <div class="col-12 col-md-6 offset-md-3">
                <div class="card">
                    <div class="card-body text-center">
                        <h5 class="card-title">Device Status</h5>
                        <p class="card-text"><strong>Wi-Fi:</strong> <span id="wifi">Loading…</span></p>
                        <p class="card-text"><strong>Time:</strong> <span id="datetime">Loading…</span></p>
                        <p class="card-text"><strong>Temperature:</strong> <span id="temp">Loading…</span></p>
                        <p class="card-text"><strong>Humidity:</strong> <span id="hum">Loading…</span></p>
                        <p class="card-text"><strong>Water level:</strong> <span id="level">Loading…</span></p>
                        <a href="#" class="btn btn-primary" onclick="location.reload()">Refresh</a>
                    </div>
                </div>
            </div>
        </div>
    </div>
</div>
<script>
async function fetchStatus(){
    try{
        const r = await fetch('/status');
        const j = await r.json();
        document.getElementById('wifi').textContent = j.connected ? ('Connected: ' + j.ip) : 'Not connected';
    }catch(e){
        document.getElementById('wifi').textContent = 'Error';
    }
}
fetchStatus();
// fetch time and update every 60 seconds
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

// fetch sensor data and update every 5 seconds
async function fetchSensor(){
    try{
        const r = await fetch('/sensor');
        const j = await r.json();
        document.getElementById('temp').textContent = j.temperature !== null ? (j.temperature + ' °C') : 'N/A';
        document.getElementById('hum').textContent = j.humidity !== null ? (j.humidity + ' %') : 'N/A';
        document.getElementById('level').textContent = j.level !== null ? (j.level + ' %') : 'N/A';
    }catch(e){
        document.getElementById('temp').textContent = 'Error';
        document.getElementById('hum').textContent = 'Error';
    }
}
fetchSensor();
setInterval(fetchSensor, 5000);
</script>
</body>
</html>
)rawliteral";
