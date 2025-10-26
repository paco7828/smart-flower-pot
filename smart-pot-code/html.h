#pragma once

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <title>Smart Flower Pot Setup</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    body {
      font-family: sans-serif;
      background-color: #f0f0f0;
      text-align: center;
      padding: 2em;
      margin: 0;
    }
    .container {
      max-width: 450px;
      margin: 0 auto;
      margin-bottom: 400px;
    }
    form {
      background: white;
      padding: 2em;
      border-radius: 10px;
      box-shadow: 0 2px 10px rgba(0,0,0,0.2);
    }
    .section {
      margin: 2em 0;
      padding: 1em;
      border: 1px solid #e0e0e0;
      border-radius: 8px;
      background-color: #fafafa;
    }
    .section h3 {
      margin-top: 0;
      color: #333;
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
      border: 1px solid #ccc;
      font-size: 1em;
      box-sizing: border-box;
    }
    input:focus {
      outline: none;
      border-color: #28a745;
      box-shadow: 0 0 5px rgba(40, 167, 69, 0.3);
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
      color: white;
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
      color: #666;
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
    <h2>Smart Flower Pot Setup</h2>
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
          <div class="input-row">
            <input type="text" name="mqtt_server" id="mqtt_server" placeholder="MQTT Server IP" value="%MQTT_SERVER%" required>
            <input type="number" name="mqtt_port" id="mqtt_port" placeholder="Port" value="%MQTT_PORT%" min="1" max="65535" required>
          </div>
          <div class="small-text">Server IP address and port number</div>
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
      <div class="error" id="error">Failed to save configuration. Please try again.</div>
    </form>
  </div>

  <script>
    document.getElementById('configForm').addEventListener('submit', function(e) {
      e.preventDefault();
      
      const submitBtn = document.getElementById('submitBtn');
      const loading = document.getElementById('loading');
      const error = document.getElementById('error');
      
      // Hide error message
      error.style.display = 'none';
      
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
          loading.innerHTML = 'Configuration saved! Device restarting...';
          setTimeout(() => {
            // Try to close the captive portal page
            if (window.opener) {
              window.close();
            } else {
              // If we can't close, redirect to a blank page
              document.body.innerHTML = '<div style="text-align:center; padding:2em; font-family:sans-serif;"><h2>Configuration saved!</h2><p>You can now close this window/tab.</p><p>The device will connect to your network and MQTT broker shortly.</p></div>';
            }
          }, 3000);
        } else {
          throw new Error('Network response was not ok');
        }
      })
      .catch(error => {
        console.error('Error:', error);
        // Show error message
        loading.style.display = 'none';
        document.getElementById('error').style.display = 'block';
        submitBtn.disabled = false;
      });
    });

    // Auto-focus on WiFi SSID input
    document.getElementById('wifi_ssid').focus();
  </script>
</body>
</html>
)rawliteral";

const char success_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <title>Configuration Saved</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    body {
      font-family: sans-serif;
      background-color: #e0ffe0;
      text-align: center;
      padding: 2em;
      margin: 0;
    }
    .message {
      background: white;
      padding: 2em;
      display: inline-block;
      border-radius: 10px;
      box-shadow: 0 2px 10px rgba(0,0,0,0.2);
      max-width: 400px;
    }
    .spinner {
      border: 4px solid #f3f3f3;
      border-radius: 50%;
      border-top: 4px solid #28a745;
      width: 30px;
      height: 30px;
      animation: spin 1s linear infinite;
      margin: 1em auto;
    }
    @keyframes spin {
      0% { transform: rotate(0deg); }
      100% { transform: rotate(360deg); }
    }
  </style>
  <script>
    // Auto-close after 5 seconds
    setTimeout(function() {
      if (window.opener) {
        window.close();
      } else {
        document.body.innerHTML = '<div class="message"><h2>Success!</h2><p>You can now close this window/tab.</p><p>The device is connecting to your WiFi network and MQTT broker.</p></div>';
      }
    }, 3000);
  </script>
</head>
<body>
  <div class="message">
    <h2>Configuration saved!</h2>
    <div class="spinner"></div>
    <p>The device will reboot and connect to the network.</p>
    <p>MQTT broker connection will be established shortly.</p>
    <p><small>This window will close automatically...</small></p>
  </div>
</body>
</html>
)rawliteral";