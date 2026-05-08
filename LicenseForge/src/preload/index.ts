import { contextBridge, ipcRenderer } from 'electron'

if (process.contextIsolated) {
  try {
    contextBridge.exposeInMainWorld('api', {
      fetchFingerprint: (config: any) => ipcRenderer.invoke('ssh-fetch-fingerprint', config),
      generateLicense: (data: any) => ipcRenderer.invoke('generate-license', data),
      getLocalFingerprint: () => ipcRenderer.invoke('get-local-fingerprint'),
      installLocalLicense: (data: string) => ipcRenderer.invoke('install-local-license', data),
      removeLocalLicense: () => ipcRenderer.invoke('remove-local-license'),
      checkLocalLicense: () => ipcRenderer.invoke('check-local-license')
    })
  } catch (error) {
    console.error(error)
  }
} else {
  // @ts-ignore (define in dts)
  window.api = {
    fetchFingerprint: (config: any) => ipcRenderer.invoke('ssh-fetch-fingerprint', config),
    generateLicense: (data: any) => ipcRenderer.invoke('generate-license', data),
    getLocalFingerprint: () => ipcRenderer.invoke('get-local-fingerprint'),
    installLocalLicense: (data: string) => ipcRenderer.invoke('install-local-license', data),
    removeLocalLicense: () => ipcRenderer.invoke('remove-local-license'),
    checkLocalLicense: () => ipcRenderer.invoke('check-local-license')
  }
}