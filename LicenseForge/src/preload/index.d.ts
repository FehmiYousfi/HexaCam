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
  }>;
  getLocalFingerprint: () => Promise<{
    success: boolean;
    fingerprint?: string;
    error?: string;
  }>;
  installLocalLicense: (data: string) => Promise<{
    success: boolean;
    path?: string;
    error?: string;
  }>;
  removeLocalLicense: () => Promise<{
    success: boolean;
    message?: string;
    error?: string;
  }>;
  checkLocalLicense: () => Promise<{
    success: boolean;
    installed: boolean;
    valid?: boolean;
    path?: string;
    size?: number;
    error?: string;
  }>;
}

declare global {
  interface Window {
    api: IElectronAPI
  }
}