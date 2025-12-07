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
        <div id="status" class="mb-3">
            <strong>Status:</strong> <span id="wifi">Loading…</span>
        </div>
        <a href="#" class="btn btn-primary" onclick="location.reload()">Refresh</a>
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
</script>
</body>
</html>
)rawliteral";
