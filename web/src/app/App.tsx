import { useCallback, useState } from 'react'
import { Confirm } from '../components/Dialog'
import { Toasts } from '../components/Toasts'
import { Login } from '../pages/Login'
import { Wizard } from '../pages/Wizard'
import { Shell } from './Shell'
import { useStore } from './store'
import { UiProvider, type AskOpts } from './ui'

function Body() {
  const auth = useStore((s) => s.auth)
  const setupDone = useStore((s) => s.config?.setup_done)
  const wizard = useStore((s) => s.status.setup?.active)
  if (auth === 'checking') return null
  if (auth === 'login') return <Login />
  // Old firmware without setup_done counts as set up; the wizard is only for a device that says it is fresh.
  if (setupDone === false || wizard) return <Wizard />
  return <Shell />
}

export function App() {
  const [dlg, setDlg] = useState<(AskOpts & { res: (v: boolean) => void }) | null>(null)
  const ask = useCallback((o: AskOpts) => new Promise<boolean>((res) => setDlg({ ...o, res })), [])
  return (
    <UiProvider onAsk={ask}>
      <Body />
      {dlg && (
        <Confirm
          d={dlg}
          done={(v) => {
            dlg.res(v)
            setDlg(null)
          }}
        />
      )}
      <Toasts />
    </UiProvider>
  )
}
