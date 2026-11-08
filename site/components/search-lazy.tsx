'use client'
import dynamic from 'next/dynamic'

export const Search = dynamic(() => import('./search'), { ssr: false })
