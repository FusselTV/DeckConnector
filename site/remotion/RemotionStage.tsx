'use client'
import dynamic from 'next/dynamic'
import type { CompositionId } from '.'

export type StageProps = { id: CompositionId; className?: string }

const Stage = dynamic(() => import('./Stage'), { ssr: false, loading: () => null })

export function RemotionStage(props: StageProps) {
  return (
    <div className={props.className}>
      <Stage {...props} />
    </div>
  )
}
