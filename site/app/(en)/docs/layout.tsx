import type { ReactNode } from 'react'
import { Docs } from '~/components/docs-layout'
import { source } from '~/lib/source'

export default function Layout({ children }: { children: ReactNode }) {
  return (
    <Docs lang="en" tree={source.getPageTree('en')}>
      {children}
    </Docs>
  )
}
