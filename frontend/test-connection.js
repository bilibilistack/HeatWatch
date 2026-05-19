const fs = require('fs');
const path = require('path');

// Manually parse .env.local to avoid needing any external dotenv dependency
function loadEnv() {
  const envPath = path.join(__dirname, '.env.local');
  if (!fs.existsSync(envPath)) {
    console.error('❌ .env.local 文件不存在！请先创建它并填写您的凭据。');
    process.exit(1);
  }

  const envContent = fs.readFileSync(envPath, 'utf8');
  const config = {};
  
  envContent.split('\n').forEach(line => {
    const trimmed = line.trim();
    if (!trimmed || trimmed.startsWith('#')) return;
    const parts = trimmed.split('=');
    if (parts.length >= 2) {
      const key = parts[0].trim();
      const value = parts.slice(1).join('=').trim().replace(/^['"]|['"]$/g, '');
      config[key] = value;
    }
  });

  return config;
}

async function testConnection() {
  const env = loadEnv();
  const baseUrl = env.THINGSBOARD_URL || 'https://thingsboard.cloud';
  const username = env.THINGSBOARD_USERNAME;
  const password = env.THINGSBOARD_PASSWORD;
  const testDeviceId = env.THINGSBOARD_DEVICE_ID;

  if (!username || username === 'your_email@example.com' || !password || password === 'your_new_password') {
    console.error('❌ 请先在 .env.local 中填写您真实的 ThingsBoard 账号邮箱和重设的密码！');
    process.exit(1);
  }

  console.log(`🌐 正在尝试连接 ThingsBoard Cloud: ${baseUrl}...`);
  console.log(`👤 登录邮箱: ${username}`);

  let token = '';

  // 1. 测试登录获取 Token
  try {
    const loginResponse = await fetch(`${baseUrl}/api/auth/login`, {
      method: 'POST',
      headers: {
        'Content-Type': 'application/json',
      },
      body: JSON.stringify({ username, password })
    });

    if (!loginResponse.ok) {
      const errText = await loginResponse.text();
      throw new Error(`HTTP ${loginResponse.status}: ${errText || '登录失败，请检查账号密码'}`);
    }

    const loginData = await loginResponse.json();
    token = loginData.token;
    console.log('✅ 登录成功！成功换取 JWT 访问 Token！');
  } catch (error) {
    console.error('❌ 登录认证失败，错误原因:');
    console.error(error.message);
    process.exit(1);
  }

  // 2. 测试获取设备列表
  try {
    console.log('\n🔍 正在获取您的设备列表...');
    const devicesResponse = await fetch(`${baseUrl}/api/tenant/devices?pageSize=20&page=0`, {
      method: 'GET',
      headers: {
        'Content-Type': 'application/json',
        'X-Authorization': `Bearer ${token}`
      }
    });

    if (!devicesResponse.ok) {
      throw new Error(`获取设备失败 HTTP ${devicesResponse.status}`);
    }

    const devicesData = await devicesResponse.json();
    const devices = devicesData.data || [];
    
    console.log(`🎉 成功获取到 ${devices.length} 个设备！`);
    if (devices.length > 0) {
      console.log('📋 设备列表预览:');
      devices.forEach((dev, idx) => {
        console.log(`   [${idx + 1}] 名字: "${dev.name}" | 类型: "${dev.type}" | ID: ${dev.id.id}`);
      });
    } else {
      console.log('⚠️ 您的租户下目前没有任何设备，请在 ThingsBoard Cloud 上添加设备，或将模拟器设备接入！');
    }
  } catch (error) {
    console.error('❌ 获取设备列表失败，错误原因:');
    console.error(error.message);
  }

  // 3. 测试特定设备的遥测数据 (如果填写了 ID)
  if (testDeviceId) {
    try {
      console.log(`\n📊 正在读取测试设备 (ID: ${testDeviceId}) 的最新遥测数据...`);
      const telemetryResponse = await fetch(`${baseUrl}/api/plugins/telemetry/DEVICE/${testDeviceId}/values/timeseries`, {
        method: 'GET',
        headers: {
          'Content-Type': 'application/json',
          'X-Authorization': `Bearer ${token}`
        }
      });

      if (!telemetryResponse.ok) {
        throw new Error(`读取遥测失败 HTTP ${telemetryResponse.status}`);
      }

      const telemetryData = await telemetryResponse.json();
      console.log('✅ 遥测数据读取成功！最新数值:');
      console.log(JSON.stringify(telemetryData, null, 2));
    } catch (error) {
      console.error(`❌ 获取设备 ${testDeviceId} 遥测数据失败，错误原因:`);
      console.error(error.message);
    }
  } else {
    console.log('\n💡 提示: 如果您想测试具体设备的最新数据读取，请在 .env.local 的 THINGSBOARD_DEVICE_ID 中填入设备 ID 并重新运行。');
  }
  
  console.log('\n✨ ThingsBoard Cloud 通信测试完成！');
}

testConnection();
