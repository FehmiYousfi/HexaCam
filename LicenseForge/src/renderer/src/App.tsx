import { useState, useEffect } from 'react'
import { Button } from '@/components/ui/button'
import { Card, CardContent, CardDescription, CardHeader, CardTitle, CardFooter } from '@/components/ui/card'
import { Input } from '@/components/ui/input'
import { Label } from '@/components/ui/label'
import { Textarea } from '@/components/ui/textarea'
import { Checkbox } from '@/components/ui/checkbox'
import { Calendar } from '@/components/ui/calendar'
import { Popover, PopoverContent, PopoverTrigger } from '@/components/ui/popover'
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from '@/components/ui/select'
import { Alert, AlertDescription, AlertTitle } from '@/components/ui/alert'
import { Toaster } from '@/components/ui/toaster'
import { useToast } from '@/hooks/use-toast'
import { format } from 'date-fns'
import { CalendarIcon, CheckCircle2, AlertCircle, Server, ShieldCheck, Key, Monitor, Download, Trash2 } from 'lucide-react'
import { cn } from '@/lib/utils'

export default function App() {
  const { toast } = useToast()
  
  // SSH State
  const [sshHost, setSshHost] = useState('')
  const [sshPort, setSshPort] = useState('22')
  const [sshUser, setSshUser] = useState('root')
  const [sshPassword, setSshPassword] = useState('')
  const [sshInstallPath, setSshInstallPath] = useState('/opt/myapp/license.lic')
  const [autoInstall, setAutoInstall] = useState(false)
  const [isFetching, setIsFetching] = useState(false)

  // Local Machine State
  const [localFingerprint, setLocalFingerprint] = useState('')
  const [localLicenseStatus, setLocalLicenseStatus] = useState<{
    installed: boolean;
    valid?: boolean;
    path?: string;
    size?: number;
  } | null>(null)
  const [isLocalMode, setIsLocalMode] = useState(false)

  // Form State
  const [fingerprint, setFingerprint] = useState('')
  const [customerName, setCustomerName] = useState('')
  const [customerEmail, setCustomerEmail] = useState('')
  const [licenseType, setLicenseType] = useState('CameraSoftware')
  const [expiry, setExpiry] = useState<Date | undefined>(undefined)
  
  // Result State
  const [generatedLicense, setGeneratedLicense] = useState<{ path: string; content: string; base64?: string; sshStatus?: string } | null>(null)
  const [error, setError] = useState<string | null>(null)
  
  const checkLocalLicense = async () => {
    try {
      const result = await window.api.checkLocalLicense()
      if (result.success) {
        setLocalLicenseStatus({
          installed: result.installed,
          valid: result.valid,
          path: result.path,
          size: result.size
        })
      }
    } catch (e: any) {
      console.error('Failed to check local license:', e)
    }
  }

  const handleGetLocalFingerprint = async () => {
    try {
      const result = await window.api.getLocalFingerprint()
      if (result.success && result.fingerprint) {
        setLocalFingerprint(result.fingerprint)
        setFingerprint(result.fingerprint)
        setIsLocalMode(true)
        toast({ 
          title: "Local Fingerprint Retrieved", 
          description: "This PC's hardware UUID has been loaded." 
        })
      } else {
        toast({ 
          title: "Failed to Get Fingerprint", 
          description: result.error || "Unknown error", 
          variant: "destructive" 
        })
      }
    } catch (e: any) {
      toast({ title: "Error", description: e.message, variant: "destructive" })
    }
  }

  const handleInstallLocalLicense = async () => {
    if (!generatedLicense?.base64) {
      toast({
        title: "No License Generated",
        description: "Please generate a license first before installing.",
        variant: "destructive"
      })
      return
    }

    try {
      const result = await window.api.installLocalLicense(generatedLicense.base64)
      if (result.success) {
        toast({
          title: "License Installed",
          description: `License installed at ${result.path}`
        })
        await checkLocalLicense()
      } else {
        toast({
          title: "Installation Failed",
          description: result.error || "Unknown error",
          variant: "destructive"
        })
      }
    } catch (e: any) {
      toast({ title: "Error", description: e.message, variant: "destructive" })
    }
  }

  const handleRemoveLocalLicense = async () => {
    try {
      const result = await window.api.removeLocalLicense()
      if (result.success) {
        toast({
          title: "License Removed",
          description: result.message || "License has been removed from this PC"
        })
        await checkLocalLicense()
      } else {
        toast({
          title: "Removal Failed",
          description: result.error || "Unknown error",
          variant: "destructive"
        })
      }
    } catch (e: any) {
      toast({ title: "Error", description: e.message, variant: "destructive" })
    }
  }

  // Check local license on mount
  useEffect(() => {
    checkLocalLicense()
  }, [])
  
  const handleFetchFingerprint = async () => {
    if (!sshHost || !sshUser || !sshPassword) {
      toast({
        title: "Missing Credentials",
        description: "Host, Username, and Password are required to connect via SSH.",
        variant: "destructive"
      })
      return
    }

    setIsFetching(true)
    setError(null)
    toast({ title: "Connecting via SSH...", description: `Attempting to reach ${sshHost}:${sshPort}` })
    
    try {
      const res = await window.api.fetchFingerprint({
        host: sshHost,
        port: sshPort,
        user: sshUser,
        password: sshPassword
      })

      if (res.success && res.fingerprint) {
        setFingerprint(res.fingerprint)
        toast({ title: "Fingerprint Acquired", description: "Successfully extracted hardware UUID." })
      } else {
        toast({ title: "SSH Fetch Failed", description: res.error || "Unknown error", variant: "destructive" })
      }
    } catch (e: any) {
      toast({ title: "IPC Error", description: e.message, variant: "destructive" })
    } finally {
      setIsFetching(false)
    }
  }

  const handleGenerate = async () => {
    setError(null)
    setGeneratedLicense(null)

    if (!fingerprint || !customerName || !customerEmail) {
      toast({
        title: "Validation Error",
        description: "Please fill in all required fields (Fingerprint, Name, Email).",
        variant: "destructive"
      })
      return
    }

    try {
      const payload: any = {
        fingerprint,
        customerName,
        customerEmail,
        licenseType,
        expiry: expiry ? expiry.toISOString() : null,
      }

      if (autoInstall) {
        payload.sshConfig = {
          host: sshHost,
          port: sshPort,
          user: sshUser,
          password: sshPassword,
          installPath: sshInstallPath,
          autoInstall: true
        }
      }

      const result = await window.api.generateLicense(payload)

      if (result.success && result.path && result.content) {
        setGeneratedLicense({
          path: result.path,
          content: result.content,
          base64: result.base64,
          sshStatus: result.sshInstallStatus
        })
        toast({
          title: "Success",
          description: `Encrypted binary license saved to ${result.path}`,
        })
      } else {
        setError(result.error || 'Failed to generate license')
        toast({
          title: "Generation Failed",
          description: result.error || 'An unknown error occurred.',
          variant: "destructive"
        })
      }
    } catch (err: any) {
      setError(err.message || 'Error communicating with main process.')
    }
  }

  return (
    <div className="min-h-screen bg-slate-950 p-6 flex flex-col items-center">
      <Toaster />
      <div className="w-full max-w-4xl space-y-6">
        <div className="flex flex-col space-y-2">
          <h1 className="text-3xl font-bold tracking-tight text-white">LicenseForge</h1>
          <p className="text-slate-400">Secure Cryptographic License Generator</p>
        </div>

        <div className="grid grid-cols-1 md:grid-cols-[1fr_1fr] gap-6 items-start">
          
          <div className="space-y-6">
            {/* Local Machine Card */}
            <Card className="bg-slate-900 border-slate-800 text-white shadow-xl">
              <CardHeader className="pb-3 border-b border-slate-800">
                <CardTitle className="text-lg flex items-center gap-2">
                  <Monitor className="w-5 h-5 text-green-400" />
                  This PC (Local Machine)
                </CardTitle>
                <CardDescription className="text-slate-400">
                  Generate and manage license for this computer.
                </CardDescription>
              </CardHeader>
              <CardContent className="space-y-4 pt-4">
                <div className="space-y-2">
                  <Label>Local Machine Fingerprint</Label>
                  <div className="flex gap-2">
                    <Input
                      readOnly
                      placeholder="Click button to retrieve..."
                      className="font-mono text-sm bg-slate-950 border-slate-700 text-slate-300"
                      value={localFingerprint}
                    />
                    <Button
                      variant="secondary"
                      className="bg-slate-800 hover:bg-slate-700 text-white flex items-center gap-2 shrink-0"
                      onClick={handleGetLocalFingerprint}
                    >
                      <Key className="w-4 h-4" />
                      Get
                    </Button>
                  </div>
                </div>

                {localLicenseStatus && (
                  <div className="pt-2 border-t border-slate-800">
                    <Label className="text-xs text-slate-400 uppercase tracking-wider mb-2 block">
                      License Status
                    </Label>
                    {localLicenseStatus.installed ? (
                      <div className="space-y-2">
                        <div className={cn(
                          "text-sm font-medium px-3 py-2 rounded flex items-center gap-2",
                          localLicenseStatus.valid ? "bg-green-950/50 text-green-400" : "bg-red-950/50 text-red-400"
                        )}>
                          {localLicenseStatus.valid ? (
                            <>
                              <CheckCircle2 className="w-4 h-4" />
                              License Installed
                            </>
                          ) : (
                            <>
                              <AlertCircle className="w-4 h-4" />
                              Invalid License Format
                            </>
                          )}
                        </div>
                        {localLicenseStatus.path && (
                          <div className="text-xs text-slate-400 break-all">
                            Path: {localLicenseStatus.path}
                          </div>
                        )}
                        {localLicenseStatus.size && (
                          <div className="text-xs text-slate-400">
                            Size: {localLicenseStatus.size} bytes
                          </div>
                        )}
                        <div className="flex gap-2 pt-2">
                          <Button
                            variant="destructive"
                            size="sm"
                            className="flex-1 flex items-center gap-2"
                            onClick={handleRemoveLocalLicense}
                          >
                            <Trash2 className="w-4 h-4" />
                            Remove License
                          </Button>
                        </div>
                      </div>
                    ) : (
                      <div className="space-y-2">
                        <div className="text-sm font-medium px-3 py-2 rounded flex items-center gap-2 bg-slate-950/50 text-slate-400">
                          <AlertCircle className="w-4 h-4" />
                          No License Installed
                        </div>
                        {generatedLicense && isLocalMode && (
                          <Button
                            variant="default"
                            size="sm"
                            className="w-full bg-green-600 hover:bg-green-700 flex items-center gap-2"
                            onClick={handleInstallLocalLicense}
                          >
                            <Download className="w-4 h-4" />
                            Install Generated License
                          </Button>
                        )}
                      </div>
                    )}
                  </div>
                )}
              </CardContent>
            </Card>

            {/* Target Machine SSH Card */}
            <Card className="bg-slate-900 border-slate-800 text-white shadow-xl">
              <CardHeader className="pb-3 border-b border-slate-800">
                <CardTitle className="text-lg flex items-center gap-2">
                  <Server className="w-5 h-5 text-blue-400" />
                  Target Machine Settings (SSH)
                </CardTitle>
                <CardDescription className="text-slate-400">
                  Provide credentials to auto-fetch fingerprint and optionally install.
                </CardDescription>
              </CardHeader>
              <CardContent className="space-y-4 pt-4">
                <div className="grid grid-cols-[1fr_80px] gap-4">
                  <div className="space-y-2">
                    <Label>Host / IP</Label>
                    <Input
                      placeholder="192.168.1.10"
                      className="bg-slate-950 border-slate-700 text-sm h-8"
                      value={sshHost} onChange={(e) => setSshHost(e.target.value)}
                    />
                  </div>
                  <div className="space-y-2">
                    <Label>Port</Label>
                    <Input
                      className="bg-slate-950 border-slate-700 text-sm h-8"
                      value={sshPort} onChange={(e) => setSshPort(e.target.value)}
                    />
                  </div>
                </div>
                <div className="grid grid-cols-2 gap-4">
                  <div className="space-y-2">
                    <Label>Username</Label>
                    <Input
                      className="bg-slate-950 border-slate-700 text-sm h-8"
                      value={sshUser} onChange={(e) => setSshUser(e.target.value)}
                    />
                  </div>
                  <div className="space-y-2">
                    <Label>Password</Label>
                    <Input
                      type="password"
                      className="bg-slate-950 border-slate-700 text-sm h-8"
                      value={sshPassword} onChange={(e) => setSshPassword(e.target.value)}
                    />
                  </div>
                </div>

                <div className="pt-2 flex justify-end">
                  <Button
                    variant="secondary"
                    className="w-full bg-slate-800 hover:bg-slate-700 text-white flex items-center gap-2 h-9"
                    onClick={handleFetchFingerprint}
                    disabled={isFetching}
                  >
                    <Key className="w-4 h-4" />
                    {isFetching ? "Fetching..." : "Fetch Machine Fingerprint"}
                  </Button>
                </div>

                <div className="pt-2 border-t border-slate-800 mt-2 space-y-3">
                  <div className="flex items-center space-x-2">
                    <Checkbox
                      id="auto-install"
                      checked={autoInstall}
                      onCheckedChange={(c) => setAutoInstall(!!c)}
                      className="border-slate-500 data-[state=checked]:bg-green-600"
                    />
                    <label htmlFor="auto-install" className="text-sm font-medium cursor-pointer text-slate-300">
                      Auto-install license over SSH after generation
                    </label>
                  </div>
                  {autoInstall && (
                    <div className="space-y-2 animate-in fade-in slide-in-from-top-1">
                      <Label>Remote Installation Path</Label>
                      <Input
                        placeholder="/opt/myapp/license.lic"
                        className="bg-slate-950 border-slate-700 text-sm h-8 text-slate-300"
                        value={sshInstallPath} onChange={(e) => setSshInstallPath(e.target.value)}
                      />
                    </div>
                  )}
                </div>
              </CardContent>
            </Card>

            {/* License Details Card */}
            <Card className="bg-slate-900 border-slate-800 text-white shadow-xl">
              <CardHeader className="pb-3 border-b border-slate-800">
                <CardTitle className="text-lg flex items-center gap-2">
                  <ShieldCheck className="w-5 h-5 text-purple-400" />
                  License Details
                </CardTitle>
                <CardDescription className="text-slate-400">
                  Enter customer info and adjust required features.
                </CardDescription>
              </CardHeader>
              <CardContent className="space-y-4 pt-4">
                <div className="space-y-2">
                  <Label htmlFor="fingerprint">Machine Fingerprint (UUID) <span className="text-red-500">*</span></Label>
                  <Input
                    id="fingerprint"
                    placeholder="Fetch from SSH or paste here..."
                    className="font-mono text-sm bg-slate-950 border-slate-700"
                    value={fingerprint}
                    onChange={(e: React.ChangeEvent<HTMLInputElement>) => setFingerprint(e.target.value)}
                  />
                </div>

                <div className="grid grid-cols-2 gap-4">
                  <div className="space-y-2">
                    <Label htmlFor="name">Customer Name <span className="text-red-500">*</span></Label>
                    <Input
                      id="name"
                      placeholder="John Doe"
                      className="bg-slate-950 border-slate-700"
                      value={customerName}
                      onChange={(e: React.ChangeEvent<HTMLInputElement>) => setCustomerName(e.target.value)}
                    />
                  </div>
                  <div className="space-y-2">
                    <Label htmlFor="email">Customer Email <span className="text-red-500">*</span></Label>
                    <Input
                      id="email"
                      type="email"
                      placeholder="john@example.com"
                      className="bg-slate-950 border-slate-700"
                      value={customerEmail}
                      onChange={(e: React.ChangeEvent<HTMLInputElement>) => setCustomerEmail(e.target.value)}
                    />
                  </div>
                </div>

                <div className="grid grid-cols-2 gap-4">
                  <div className="space-y-2">
                    <Label htmlFor="type">License Type</Label>
                    <Select value={licenseType} onValueChange={setLicenseType}>
                      <SelectTrigger className="bg-slate-950 border-slate-700 text-white">
                        <SelectValue placeholder="Select license type" />
                      </SelectTrigger>
                      <SelectContent className="bg-slate-900 border-slate-700">
                        <SelectItem value="CameraSoftware" className="text-white hover:bg-slate-800">
                          CameraSoftware
                        </SelectItem>
                        <SelectItem value="GcontrolStation" className="text-white hover:bg-slate-800">
                          GcontrolStation
                        </SelectItem>
                      </SelectContent>
                    </Select>
                  </div>

                  <div className="space-y-2">
                    <Label>Expiry Date <span className="text-slate-500 font-normal text-xs">(Optional)</span></Label>
                    <Popover>
                      <PopoverTrigger asChild>
                        <Button
                          variant={"outline"}
                          className={cn(
                            "w-full justify-start text-left font-normal bg-slate-950 border-slate-700",
                            !expiry && "text-muted-foreground"
                          )}
                        >
                          <CalendarIcon className="mr-2 h-4 w-4" />
                          {expiry ? format(expiry, "PPP") : <span>No Expiry (Perpetual)</span>}
                        </Button>
                      </PopoverTrigger>
                      <PopoverContent className="w-auto p-0 bg-slate-900 border-slate-700" align="start">
                        <Calendar
                          mode="single"
                          selected={expiry}
                          onSelect={setExpiry}
                          initialFocus
                          className="bg-slate-900 text-white"
                        />
                      </PopoverContent>
                    </Popover>
                  </div>
                </div>
              </CardContent>
              <CardFooter className="pt-2">
                <Button onClick={handleGenerate} className="w-full bg-blue-600 hover:bg-blue-700 text-white font-semibold py-6 text-lg flex items-center gap-2">
                  <ShieldCheck className="w-5 h-5" />
                  Generate Secure License
                </Button>
              </CardFooter>
            </Card>
          </div>

          <div className="space-y-6 flex flex-col h-full">
            {error && (
              <Alert variant="destructive" className="bg-red-950/50 border-red-900 text-red-200">
                <AlertCircle className="h-4 w-4" />
                <AlertTitle>Error</AlertTitle>
                <AlertDescription>{error}</AlertDescription>
              </Alert>
            )}

            {generatedLicense ? (
              <Card className="bg-slate-900 border-slate-800 text-white shadow-xl flex-1 flex flex-col">
                <CardHeader>
                  <CardTitle className="flex items-center text-green-400 gap-2">
                    <CheckCircle2 className="h-5 w-5" />
                    Encrypted License Generated
                  </CardTitle>
                  <CardDescription className="text-slate-400 break-all">
                    Secure Binary Saved to: <br/> <code className="text-xs bg-slate-950 p-1 rounded mt-1 block">{generatedLicense.path}</code>
                  </CardDescription>
                </CardHeader>
                <CardContent className="flex-1 flex flex-col space-y-4">
                  <div className="flex-1 flex flex-col">
                    <Label className="mb-2 text-xs text-slate-400 uppercase tracking-wider">Hex Dump Preview (LICF Magic Bytes + IV + Auth + Encrypted Data)</Label>
                    <Textarea
                      readOnly
                      value={generatedLicense.content}
                      className="font-mono text-[10px] leading-tight bg-slate-950 border-slate-800 text-green-500/80 flex-1 resize-none min-h-[120px]"
                    />
                  </div>
                  <div className="flex flex-col">
                    <Label className="mb-2 text-xs text-slate-400 uppercase tracking-wider">Base64 Representation</Label>
                    <Textarea
                      readOnly
                      value={generatedLicense.base64}
                      className="font-mono text-xs bg-slate-950 border-slate-800 text-blue-400/80 h-24 resize-none break-all"
                    />
                  </div>
                  {generatedLicense.sshStatus && (
                    <div className="flex flex-col mt-4 pt-4 border-t border-slate-800">
                      <Label className="mb-2 text-xs text-slate-400 uppercase tracking-wider">SSH Installation Status</Label>
                      <div className={cn(
                        "text-sm font-medium px-3 py-2 rounded flex items-center gap-2",
                        generatedLicense.sshStatus === 'Success' ? "bg-green-950/50 text-green-400" : "bg-red-950/50 text-red-400"
                      )}>
                        {generatedLicense.sshStatus === 'Success' ? <CheckCircle2 className="w-4 h-4"/> : <AlertCircle className="w-4 h-4"/>}
                        {generatedLicense.sshStatus}
                      </div>
                    </div>
                  )}
                </CardContent>
              </Card>
            ) : (
              <Card className="bg-slate-900/50 border-slate-800 border-dashed text-white shadow-xl flex-1 flex items-center justify-center text-slate-500 text-sm p-6 text-center">
                Fill the form and click "Generate Secure License" to view the cryptographic output here.
              </Card>
            )}
          </div>
        </div>
      </div>
    </div>
  )
}