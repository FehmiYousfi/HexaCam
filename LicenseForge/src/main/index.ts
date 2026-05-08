import { app, shell, BrowserWindow, ipcMain } from 'electron'
import { join } from 'path'
import { electronApp, optimizer, is } from '@electron-toolkit/utils'
import * as crypto from 'crypto'
import * as fs from 'fs'
import * as os from 'os'
import * as dotenv from 'dotenv'
import { Client } from 'ssh2'

// Load environment variables (like PRIVATE_KEY_PATH)
dotenv.config()

function createWindow(): void {
  const mainWindow = new BrowserWindow({
    width: 900,
    height: 700,
    show: false,
    autoHideMenuBar: true,
    webPreferences: {
      preload: join(__dirname, '../preload/index.js'),
      sandbox: false,
      contextIsolation: true,
      nodeIntegration: false
    }
  })

  mainWindow.on('ready-to-show', () => {
    mainWindow.show()
  })

  mainWindow.webContents.setWindowOpenHandler((details) => {
    shell.openExternal(details.url)
    return { action: 'deny' }
  })

  if (is.dev && process.env['ELECTRON_RENDERER_URL']) {
    mainWindow.loadURL(process.env['ELECTRON_RENDERER_URL'])
  } else {
    mainWindow.loadFile(join(__dirname, '../renderer/index.html'))
  }
}

app.whenReady().then(() => {
  electronApp.setAppUserModelId('com.licenseforge.app')
  app.on('browser-window-created', (_, window) => {
    optimizer.watchWindowShortcuts(window)
  })

  createWindow()

  app.on('activate', function () {
    if (BrowserWindow.getAllWindows().length === 0) createWindow()
  })
})

app.on('window-all-closed', () => {
  if (process.platform !== 'darwin') {
    app.quit()
  }
})

// ==========================================
// Local Machine Fingerprint Logic
// ==========================================
ipcMain.handle('get-local-fingerprint', async () => {
  try {
    let fingerprint = ''
    
    if (process.platform === 'linux') {
      // Try product_uuid first, fallback to machine-id
      try {
        fingerprint = fs.readFileSync('/sys/class/dmi/id/product_uuid', 'utf8').trim()
      } catch {
        try {
          fingerprint = fs.readFileSync('/etc/machine-id', 'utf8').trim()
        } catch {
          throw new Error('Could not read machine fingerprint')
        }
      }
    } else if (process.platform === 'win32') {
      // Windows: Use machine GUID
      const { execSync } = require('child_process')
      fingerprint = execSync('wmic csproduct get uuid', { encoding: 'utf8' })
        .split('\n')[1]
        .trim()
    } else if (process.platform === 'darwin') {
      // macOS: Use hardware UUID
      const { execSync } = require('child_process')
      fingerprint = execSync('ioreg -rd1 -c IOPlatformExpertDevice | grep IOPlatformUUID', { encoding: 'utf8' })
        .split('=')[1]
        .trim()
        .replace(/"/g, '')
    }
    
    if (!fingerprint) {
      throw new Error('Could not determine machine fingerprint')
    }
    
    return { success: true, fingerprint }
  } catch (error: any) {
    return { success: false, error: error.message }
  }
})

// ==========================================
// Local License Management
// ==========================================
const getLocalLicensePath = () => {
  const homeDir = os.homedir()
  const licenseDir = join(homeDir, '.licenseforge')
  if (!fs.existsSync(licenseDir)) {
    fs.mkdirSync(licenseDir, { recursive: true })
  }
  return join(licenseDir, 'local_license.lic')
}

ipcMain.handle('install-local-license', async (_, licenseData) => {
  try {
    const licensePath = getLocalLicensePath()
    fs.writeFileSync(licensePath, Buffer.from(licenseData, 'base64'))
    return { success: true, path: licensePath }
  } catch (error: any) {
    return { success: false, error: error.message }
  }
})

ipcMain.handle('remove-local-license', async () => {
  try {
    const licensePath = getLocalLicensePath()
    if (fs.existsSync(licensePath)) {
      fs.unlinkSync(licensePath)
      return { success: true, message: 'License removed successfully' }
    } else {
      return { success: false, error: 'No license file found' }
    }
  } catch (error: any) {
    return { success: false, error: error.message }
  }
})

ipcMain.handle('check-local-license', async () => {
  try {
    const licensePath = getLocalLicensePath()
    const exists = fs.existsSync(licensePath)
    
    if (!exists) {
      return { success: true, installed: false }
    }
    
    // Read and parse license to show details
    const licenseBuffer = fs.readFileSync(licensePath)
    const magicBytes = licenseBuffer.slice(0, 4).toString('utf8')
    
    if (magicBytes !== 'LICF') {
      return { success: true, installed: true, valid: false, error: 'Invalid license format' }
    }
    
    return { 
      success: true, 
      installed: true, 
      valid: true,
      path: licensePath,
      size: licenseBuffer.length
    }
  } catch (error: any) {
    return { success: false, error: error.message }
  }
})

// ==========================================
// SSH Fetch Fingerprint Logic
// ==========================================
ipcMain.handle('ssh-fetch-fingerprint', async (_, config) => {
  return new Promise((resolve) => {
    const conn = new Client()
    
    conn.on('ready', () => {
      // First try product_uuid (best for bound hardware), fallback to machine-id
      conn.exec('cat /sys/class/dmi/id/product_uuid 2>/dev/null || cat /etc/machine-id', (err, stream) => {
        if (err) {
          conn.end()
          return resolve({ success: false, error: err.message })
        }
        
        let output = ''
        stream.on('data', (data: Buffer) => {
          output += data.toString()
        }).on('close', () => {
          conn.end()
          const fingerprint = output.trim()
          if (!fingerprint) {
            resolve({ success: false, error: 'Could not read machine fingerprint from target system.' })
          } else {
            resolve({ success: true, fingerprint })
          }
        }).stderr.on('data', (data: Buffer) => {
          console.error('SSH Error:', data.toString())
        })
      })
    }).on('error', (err: any) => {
      resolve({ success: false, error: err.message })
    }).connect({
      host: config.host,
      port: parseInt(config.port, 10) || 22,
      username: config.user,
      password: config.password,
      readyTimeout: 10000
    })
  })
})

// IPC Handler for License Generation
ipcMain.handle('generate-license', async (_, licenseData) => {
  try {
    // 1. Prepare payload
    const payload = {
      fingerprint: licenseData.fingerprint,
      customerName: licenseData.customerName,
      customerEmail: licenseData.customerEmail,
      licenseType: licenseData.licenseType, // "CameraSoftware"
      expiry: licenseData.expiry || null,
      issuedAt: new Date().toISOString(),
      version: '1.0'
    }

    const payloadString = JSON.stringify(payload)

    // 2. Load RSA Private Key
    const keyPath = process.env.PRIVATE_KEY_PATH || join(app.getAppPath(), 'private-key.pem')
    if (!fs.existsSync(keyPath)) {
      throw new Error(`Private key not found at ${keyPath}. Please generate one.`)
    }
    const privateKey = fs.readFileSync(keyPath, 'utf8')

    // 3. Generate RSA-4096 Signature (Authenticity)
    const signer = crypto.createSign('RSA-SHA256')
    signer.update(payloadString)
    signer.end()
    const signature = signer.sign(privateKey, 'base64')

    // 4. Combine Payload + Signature
    // IMPORTANT: Storing `data` as the exact string format prevents C++ JSON parsers
    // from reordering keys and breaking the RSA Signature verification.
    const signedData = JSON.stringify({
      data: payloadString,
      signature: signature
    })

    // 5. Hardware-Bound Key Derivation (AES-256-GCM Key)
    // We hash the Machine Fingerprint to get a deterministic 32-byte key
    const machineKey = crypto.createHash('sha256').update(licenseData.fingerprint).digest()

    // 6. Encrypt with AES-256-GCM (Confidentiality & Integrity)
    const iv = crypto.randomBytes(12) // Recommended 12-byte IV for GCM
    const cipher = crypto.createCipheriv('aes-256-gcm', machineKey, iv)
    
    let encryptedData = cipher.update(signedData, 'utf8')
    const finalBuffer = cipher.final()
    encryptedData = Buffer.concat([encryptedData, finalBuffer])
    const authTag = cipher.getAuthTag() // 16 bytes

    // 7. Binary Packing (.lic format)
    // Format: Magic Bytes (4) + Version (1) + IV (12) + AuthTag (16) + Ciphertext (Variable)
    const magicBytes = Buffer.from('LICF', 'utf8')
    const versionByte = Buffer.from([0x01])
    
    const binaryLicense = Buffer.concat([
      magicBytes,
      versionByte,
      iv,
      authTag,
      encryptedData
    ])

    // 8. Ensure Output Directory Exists
    const homeDir = os.homedir()
    const outDir = join(homeDir, 'LicenseForge', 'Generated')
    if (!fs.existsSync(outDir)) {
      fs.mkdirSync(outDir, { recursive: true })
    }

    // 9. Save Binary File Locally
    const safeName = payload.customerName.replace(/[^a-z0-9]/gi, '_').toLowerCase()
    const filePath = join(outDir, `${safeName}_encrypted.lic`)
    fs.writeFileSync(filePath, binaryLicense)

    // 10. Auto-Install via SSH if requested
    let sshInstallStatus = 'Skipped'
    if (licenseData.sshConfig && licenseData.sshConfig.autoInstall && licenseData.sshConfig.installPath) {
      sshInstallStatus = await new Promise((resolve) => {
        const conn = new Client()
        conn.on('ready', () => {
          conn.sftp((err, sftp) => {
            if (err) {
              conn.end()
              return resolve(`Failed to open SFTP: ${err.message}`)
            }
            sftp.writeFile(licenseData.sshConfig.installPath, binaryLicense, (err: any) => {
              conn.end()
              if (err) resolve(`Upload Failed: ${err.message}`)
              else resolve('Success')
            })
          })
        }).on('error', (err: any) => {
          resolve(`Connection Error: ${err.message}`)
        }).connect({
          host: licenseData.sshConfig.host,
          port: parseInt(licenseData.sshConfig.port, 10) || 22,
          username: licenseData.sshConfig.user,
          password: licenseData.sshConfig.password,
          readyTimeout: 10000
        })
      })
    }

    // Format output for UI (Hex Dump preview)
    const hexPreview = binaryLicense.toString('hex').match(/.{1,32}/g)?.join('\n') || ''

    return {
      success: true,
      path: filePath,
      content: hexPreview,
      base64: binaryLicense.toString('base64'),
      sshInstallStatus
    }
  } catch (error: any) {
    console.error('License Generation Error:', error)
    return {
      success: false,
      error: error.message || 'Unknown error occurred during generation'
    }
  }
})