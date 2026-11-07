import { useCallback, useRef, useState } from 'react'
import { Confirm } from '../components/Dialog'
import { Toasts } from '../components/Toasts'
import { Login } from '../pages/Login'
import { Wizard } from '../pages/Wizard'
import { Shell } from './Shell'
import { useStore } from './store'
import { UiProvider, type AskOpts } from './ui'

function Body() {
  const auth = useStore((s) => s.auth)
  const wizard = useStore((s) => s.status.setup?.active)
  if (auth === 'checking') return null
  if (auth === 'login') return <Login />
  if (wizard) return <Wizard />
  return <Shell />
}

type Dlg = AskOpts & { res: (v: boolean) => void; n: number }

export function App() {
  const [dlg, setDlg] = useState<Dlg | null>(null)
  const open = useRef<Dlg | null>(null)
  // A new question cancels the one still open, so no caller waits forever.
  const ask = useCallback(
    (o: AskOpts) =>
      new Promise<boolean>((res) => {
        open.current?.res(false)
        open.current = { ...o, res, n: (open.current?.n ?? 0) + 1 }
        setDlg(open.current)
      }),
    [],
  )
  return (
    <UiProvider onAsk={ask}>
      <Body />
      {dlg && (
        <Confirm
          key={dlg.n}
          d={dlg}
          done={(v) => {
            dlg.res(v)
            if (open.current === dlg) open.current = null
            setDlg(null)
          }}
        />
      )}
      <Toasts />
    </UiProvider>
  )
}
