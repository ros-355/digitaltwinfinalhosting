import eventlet
eventlet.monkey_patch()

# Your other imports go down here...
from flask import Flask, render_template
import eventlet
eventlet.monkey_patch()
import os
import json
import paho.mqtt.client as mqtt
# Added: flash for the error message display
from flask import Flask, render_template, send_file, make_response, redirect, url_for, request, flash
from flask_socketio import SocketIO
from threading import Thread
import firebase_admin
from firebase_admin import credentials, db as firebase_db
from datetime import datetime

# --- NEW ML IMPORTS ---
import numpy as np
from sklearn.linear_model import LinearRegression

# --- REQUIRED FOR REPORT GRAPH ---
import matplotlib
matplotlib.use('Agg') 
import matplotlib.pyplot as plt
import io
import base64

# --- ADDED: LOGIN IMPORTS ---
from flask_login import LoginManager, UserMixin, login_user, login_required, logout_user, current_user

# --- CONFIGURATION ---
MQTT_BROKER = "broker.hivemq.com"
FIREBASE_URL = "https://digitaltwinfinal1-default-rtdb.firebaseio.com/"
KEY_PATH = "serviceAccountKey.json"

app = Flask(__name__)
# Added: Secret key is required for sessions/login
app.config['SECRET_KEY'] = 'ioe_substation_secret_key'

socketio = SocketIO(app, async_mode='eventlet', cors_allowed_origins="*")

# --- ADDED: LOGIN MANAGER SETUP ---
login_manager = LoginManager()
login_manager.init_app(app)
login_manager.login_view = 'login'

# Simple User Class
class User(UserMixin):
    def __init__(self, id):
        self.id = id

# Mock Database (User ID: admin)
users = {'admin': User('admin')}

@login_manager.user_loader
def load_user(user_id):
    return users.get(user_id)

substation_data = {
    "v_33kv": 0.0, "i_33kv": 0.0,
    "v_11kv": 0.0, "i_11kv": 0.0,
    "active_power": 0.0, "energy_kwh": 0.0,
    "coil1": 0.0, "coil2": 0.0, "coil3": 0.0,
    "timestamp": ""
}

report_history_log = []

# --- Firebase Initialization ---
cloud_ref = None
if os.path.exists(KEY_PATH):
    try:
        cred = credentials.Certificate(KEY_PATH)
        firebase_admin.initialize_app(cred, {'databaseURL': FIREBASE_URL})
        cloud_ref = firebase_db.reference('substationData') 
        print("✅ Firebase Connection: SUCCESS")
    except Exception as e:
        print(f"❌ Firebase Error: {e}")

# --- NEW ML PREDICTION FUNCTION ---
def get_ml_prediction():
    global report_history_log
    if len(report_history_log) < 5:
        return "Insufficient Data"
    
    try:
        data_points = report_history_log[::-1]
        y = np.array([row['p'] for row in data_points]).reshape(-1, 1)
        X = np.array(range(len(y))).reshape(-1, 1)

        model = LinearRegression()
        model.fit(X, y)
        
        prediction = model.predict([[len(y)]])
        return round(float(prediction[0][0]), 3)
    except Exception as e:
        print(f"ML Error: {e}")
        return "Prediction Error"

# --- REQUIRED: GENERATE GRAPH FOR REPORT ---
def generate_report_graph():
    global report_history_log
    if len(report_history_log) < 3:
        return None
    try:
        data_points = report_history_log[::-1]
        y = np.array([row['p'] for row in data_points])
        X = np.array(range(len(y))).reshape(-1, 1)

        model = LinearRegression()
        model.fit(X, y)
        y_pred = model.predict(X)

        plt.figure(figsize=(6, 4))
        plt.scatter(range(len(y)), y, color='#3498db', label='Actual Load')
        plt.plot(range(len(y)), y_pred, color='#2ecc71', label='Regression Line', linewidth=2)
        plt.title('Substation Load Trend (Linear Regression)')
        plt.xlabel('Recent Readings')
        plt.ylabel('Power (kW)')
        plt.legend()
        plt.grid(True, linestyle='--', alpha=0.6)
        
        img = io.BytesIO()
        plt.savefig(img, format='png', bbox_inches='tight')
        img.seek(0)
        plot_url = base64.b64encode(img.getvalue()).decode()
        plt.close()
        return f"data:image/png;base64,{plot_url}"
    except:
        return None

def on_message(client, userdata, msg):
    global substation_data, report_history_log
    topic = msg.topic
    try:
        payload_str = msg.payload.decode('utf-8')
        try:
            val = float(payload_str)
        except ValueError:
            return

        if topic == "transformer/primary/voltage":
            substation_data["v_33kv"] = val
        elif topic == "transformer/primary/current":
            substation_data["i_33kv"] = val
        elif topic == "transformer/secondary/voltage":
            substation_data["v_11kv"] = val
        elif topic == "transformer/secondary/total_amps":
            substation_data["i_11kv"] = val
        elif topic == "transformer/secondary/coil1":
            substation_data["coil1"] = val
        elif topic == "transformer/secondary/coil2":
            substation_data["coil2"] = val
        elif topic == "transformer/secondary/coil3":
            substation_data["coil3"] = val
        elif topic == "transformer/secondary/watts":
            substation_data["active_power"] = val / 1000.0
        elif topic == "transformer/energy/kwh":
            substation_data["energy_kwh"] = val

        substation_data["timestamp"] = datetime.now().strftime("%Y-%m-%d %H:%M:%S")

        if cloud_ref:
            cloud_ref.update(substation_data) 

        if topic == "transformer/primary/voltage":
            new_row = {
                "time": substation_data["timestamp"].split(" ")[1],
                "v33": substation_data["v_33kv"],
                "v11": substation_data["v_11kv"],
                "i33": substation_data["i_33kv"],
                "i11": substation_data["i_11kv"],
                "p": round(substation_data["active_power"], 3),
                "pf": "0.88", 
                "temp": "24.5" 
            }
            report_history_log.insert(0, new_row)
            report_history_log = report_history_log[:15]

        prediction_val = get_ml_prediction()
        if isinstance(prediction_val, (int, float)):
            socketio.emit("ml_prediction", {"val": prediction_val})

        socketio.emit("live_data", substation_data)
        print(f"📡 Received: {topic} -> {val}")

    except Exception as e:
        print(f"⚠️ Error: {e}")

def mqtt_run():
    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
    client.on_message = on_message
    client.connect(MQTT_BROKER, 1883, 60)
    client.subscribe("transformer/#")
    client.loop_forever()

# --- MODIFIED: LOGIN ROUTE WITH FLASH ---
@app.route('/login', methods=['GET', 'POST'])
def login():
    if request.method == 'POST':
        username = request.form.get('username')
        password = request.form.get('password')
        if username == 'admin' and password == 'ioe123':
            login_user(users['admin'])
            return redirect(url_for('index'))
        else:
            # Instead of returning text, we flash a message and stay on the login page
            flash('Invalid Credentials. Please try again.')
            return redirect(url_for('login'))
    return render_template('login.html')

@app.route('/logout')
@login_required
def logout():
    logout_user()
    return redirect(url_for('login'))

# --- PROTECTED ROUTES ---
@app.route('/')
@login_required
def index():
    return render_template('index.html', user_id=current_user.id)

@app.route('/history')
@login_required
def history():
    return render_template('history.html')

@app.route('/report')
@login_required
def report():
    prediction_val = get_ml_prediction()
    plot_base64 = generate_report_graph()
    
    return render_template('report.html', 
                         report_rows=report_history_log, 
                         prediction=prediction_val,
                         prediction_plot=plot_base64,
                         gen_time=datetime.now().strftime("%Y-%m-%d %H:%M:%S"))

# Use the safe SocketIO background task instead
socketio.start_background_task(mqtt_run)

if __name__ == '__main__':
    print("🌍 Web Dashboard: http://localhost:8080")
    socketio.run(app, host='0.0.0.0', port=8080, log_output=True)