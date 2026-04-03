export interface IElectronAPI {
  fetchFingerprint: (config: any) => Promise<{
    success: boolean;
    fingerprint?: string;
    error?: string;
  }>;
  generateLicense: (data: any) => Promise<{
    success: boolean;
    path?: string;
    content?: string;
    base64?: string;
    sshInstallStatus?: string;
    error?: string;
  }>
}

declare global {
  interface Window {
    api: IElectronAPI
  }
}