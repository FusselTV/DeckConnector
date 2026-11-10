import type { ComponentType } from 'react'
import { Composition, continueRender, delayRender, registerRoot, staticFile } from 'remotion'
import Compare from './Compare'
import Failover from './Failover'
import Flash from './Flash'
import Hero from './Hero'
import Hosts from './Hosts'
import { compositions, type Composition as Meta, type CompositionId } from '.'
import type { LangProps } from './text'

const scenes: Record<CompositionId, ComponentType<LangProps>> = { hero: Hero, flash: Flash, hosts: Hosts, failover: Failover, compare: Compare }

const fonts = delayRender('fonts')
void Promise.all(
  [new FontFace('Geist Variable', `url(${staticFile('geist.woff2')})`, { weight: '100 900' }), new FontFace('JetBrains Mono Variable', `url(${staticFile('jetbrains-mono.woff2')})`, { weight: '100 800' })].map(async (f) => document.fonts.add(await f.load())),
).then(() => continueRender(fonts))

function Root() {
  return (
    <>
      {Object.entries(compositions).flatMap(([id, c]: [string, Meta]) =>
        [['', c] as const, ...(c.portrait ? [['-port', c.portrait] as const] : [])].map(([v, size]) => (
          <Composition key={id + v} id={id + v} component={scenes[id as CompositionId]} durationInFrames={c.durationInFrames} fps={c.fps} width={size.width} height={size.height} defaultProps={{ lang: 'en' }} />
        )),
      )}
    </>
  )
}

registerRoot(Root)
