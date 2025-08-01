const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <title>WiFi Setup</title>
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
      max-width: 400px;
      margin: 0 auto;
    }
    form {
      background: white;
      padding: 2em;
      border-radius: 10px;
      box-shadow: 0 2px 10px rgba(0,0,0,0.2);
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
      max-width: 300px;
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
    .password-container {
      position: relative;
      display: inline-block;
      width: 100%;
      max-width: 300px;
    }
    .toggle-password {
      position: absolute;
      right: 10px;
      top: 50%;
      transform: translateY(-50%);
      background: none;
      border: none;
      cursor: pointer;
      font-size: 0.9em;
      color: #666;
      padding: 0;
      width: auto;
    }
    .toggle-password:hover {
      color: #28a745;
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
  </style>
</head>
<body>
  <div class="container">
    <h2>Smart Flower Pot WiFi Setup</h2>
    <form id="wifiForm" action="/login" method="POST">
      <div class="input-group">
        <input type="text" name="username" id="ssid" placeholder="WiFi SSID" required>
      </div>
      <div class="input-group">
        <div class="password-container">
          <input type="password" name="password" id="password" placeholder="WiFi Password" required>
          <button type="button" class="toggle-password" onclick="togglePassword()">👁️</button>
        </div>
      </div>
      <button type="submit" id="submitBtn">Save</button>
      <div class="loading" id="loading">Saving credentials...</div>
      <div class="error" id="error">Failed to save credentials. Please try again.</div>
    </form>
  </div>

  <script>
    function togglePassword() {
      const passwordInput = document.getElementById('password');
      const toggleBtn = document.querySelector('.toggle-password');
      
      if (passwordInput.type === 'password') {
        passwordInput.type = 'text';
        toggleBtn.textContent = '🙈';
      } else {
        passwordInput.type = 'password';
        toggleBtn.textContent = '👁️';
      }
    }

    document.getElementById('wifiForm').addEventListener('submit', function(e) {
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
      fetch('/login', {
        method: 'POST',
        body: formData
      })
      .then(response => {
        if (response.ok) {
          // Success - show success message and close
          loading.innerHTML = 'Credentials saved! Closing...';
          setTimeout(() => {
            // Try to close the captive portal page
            if (window.opener) {
              window.close();
            } else {
              // If we can't close, redirect to a blank page
              document.body.innerHTML = '<div style="text-align:center; padding:2em; font-family:sans-serif;"><h2>WiFi credentials saved!</h2><p>You can now close this window/tab.</p><p>The device will connect to your network shortly.</p></div>';
            }
          }, 2000);
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

    // Auto-focus on SSID input
    document.getElementById('ssid').focus();
  </script>
</body>
</html>
)rawliteral";

const char success_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <title>WiFi Saved</title>
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
    // Auto-close after 3 seconds
    setTimeout(function() {
      if (window.opener) {
        window.close();
      } else {
        document.body.innerHTML = '<div class="message"><h2>Success!</h2><p>You can now close this window/tab.</p><p>The device is connecting to your WiFi network.</p></div>';
      }
    }, 3000);
  </script>
</head>
<body>
  <div class="message">
    <h2>WiFi credentials saved!</h2>
    <div class="spinner"></div>
    <p>The device will reboot and connect to the network.</p>
    <p><small>This window will close automatically...</small></p>
  </div>
</body>
</html>
)rawliteral";