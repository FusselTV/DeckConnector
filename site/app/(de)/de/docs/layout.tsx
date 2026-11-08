import type { ReactNode } from 'react'
import { Docs } from '~/components/site-header'
import { source } from '~/lib/source'

export default function Layout({ children }: { children: ReactNode }) {
  return (
    <Docs lang="de" tree={source.getPageTree('de')}>
      {children}
    </Docs>
  )
}
