#pragma once

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <title>Smart Pot Setup</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    body {
      font-family: sans-serif;
      background-color: #000000;
      text-align: center;
      padding: 2em;
      margin: 0;
      color: #ffffff;
    }
    .container {
      max-width: 450px;
      margin: 0 auto;
      margin-bottom: 400px;
    }
    form {
      background: rgb(0, 0, 0);
      padding: 2em;
      border-radius: 10px;
      box-shadow: 0 2px 10px rgba(0,0,0,0.2);
    }
    .section {
      margin: 2em 0;
      padding: 1em;
      border: 1px solid #ffffff;
      border-radius: 8px;
      background-color: #000000;
    }
    .section h3 {
      margin-top: 0;
      color: #ffffff;
      font-size: 1.1em;
    }
    .input-group {
      position: relative;
      margin: 1em 0;
    }
    input {
      display: block;
      margin: 0 auto;
      padding: 0.7em;
      width: 100%;
      max-width: 350px;
      border-radius: 5px;
      border: 1px solid #ffffff;
      font-size: 1em;
      box-sizing: border-box;
    }
    .input-row {
      display: flex;
      gap: 10px;
      align-items: center;
      justify-content: center;
    }
    .input-row input {
      flex: 1;
      margin: 0;
    }
    .input-row input[type="number"] {
      max-width: 100px;
    }
    button[type="submit"] {
      padding: 0.7em 2em;
      border: none;
      border-radius: 5px;
      background-color: #28a745;
      color: rgb(0, 0, 0);
      font-size: 1em;
      cursor: pointer;
      margin-top: 1em;
      min-width: 120px;
    }
    button[type="submit"]:hover {
      background-color: #218838;
    }
    button[type="submit"]:disabled {
      background-color: #ccc;
      cursor: not-allowed;
    }
    .loading {
      display: none;
      margin-top: 1em;
      color: #000000;
    }
    .success {
      display: none;
      margin-top: 1em;
      color: #28a745;
      font-weight: bold;
    }
    .error {
      color: #dc3545;
      margin-top: 1em;
      display: none;
    }
    .small-text {
      font-size: 0.9em;
      color: #666;
      margin-top: 0.5em;
    }
  </style>
</head>
<body>
  <div class="container">
    <h2>Smart Pot Setup</h2>
    <form id="configForm" action="/config" method="POST">
      
      <!-- WiFi Configuration Section -->
      <div class="section">
        <h3>WiFi Configuration</h3>
        <div class="input-group">
          <input type="text" name="wifi_ssid" id="wifi_ssid" placeholder="WiFi SSID" required>
        </div>
        <div class="input-group">
          <input type="text" name="wifi_password" id="wifi_password" placeholder="WiFi Password" required>
        </div>
      </div>

      <!-- MQTT Configuration Section -->
      <div class="section">
        <h3>MQTT Broker Configuration</h3>
        <div class="input-group">
          <div class="input-group">
            <input type="text" name="mqtt_server" id="mqtt_server" placeholder="MQTT Server IP" value="%MQTT_SERVER%" required>
        </div>
        <div class="input-group">
            <input type="number" name="mqtt_port" id="mqtt_port" placeholder="MQTT Server Port" value="%MQTT_PORT%" min="1" max="65535" required>
        </div>
        </div>
        <div class="input-group">
          <input type="text" name="mqtt_username" id="mqtt_username" placeholder="MQTT Username" value="%MQTT_USER%" required>
        </div>
        <div class="input-group">
          <input type="text" name="mqtt_password" id="mqtt_password" placeholder="MQTT Password" value="%MQTT_PASS%" required>
        </div>
      </div>

      <button type="submit" id="submitBtn">Save Configuration</button>
      <div class="loading" id="loading">Saving configuration...</div>
      <div class="success" id="success">Configuration saved! Closing...</div>
      <div class="error" id="error">Failed to save configuration. Please try again.</div>
    </form>
  </div>

  <script>
    document.getElementById('configForm').addEventListener('submit', function(e) {
      e.preventDefault();
      
      const submitBtn = document.getElementById('submitBtn');
      const loading = document.getElementById('loading');
      const success = document.getElementById('success');
      const error = document.getElementById('error');
      
      // Hide messages
      error.style.display = 'none';
      success.style.display = 'none';
      
      // Show loading state
      submitBtn.disabled = true;
      loading.style.display = 'block';
      
      // Get form data
      const formData = new FormData(this);
      
      // Submit the form
      fetch('/config', {
        method: 'POST',
        body: formData
      })
      .then(response => {
        if (response.ok) {
          // Success - show success message and close
          loading.style.display = 'none';
          success.style.display = 'block';
          
          // Close window/tab after short delay
          setTimeout(() => {
            window.close();
            // If window.close() doesn't work (browser restriction), show message
            setTimeout(() => {
              success.innerHTML = 'Configuration saved!';
            }, 500);
          }, 1500);
        } else {
          throw new Error('Server returned error status: ' + response.status);
        }
      })
      .catch(error => {
        console.error('Error:', error);
        // Show error message
        loading.style.display = 'none';
        success.style.display = 'none';
        error.style.display = 'block';
        submitBtn.disabled = false;
      });
    });

    // Auto-focus on WiFi SSID input
    document.getElementById('wifi_ssid').focus();
  </script>
</body>
</html>
)rawliteral";