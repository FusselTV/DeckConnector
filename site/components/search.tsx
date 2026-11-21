'use client'
import { useEffect, useLayoutEffect, useState } from 'react'
import type { SortedResult } from 'fumadocs-core/search'
import { experimental_useSearch as useSearch } from 'fumadocs-core/search/client'
import {
  SearchDialog,
  SearchDialogClose,
  SearchDialogContent,
  SearchDialogHeader,
  SearchDialogIcon,
  SearchDialogInput,
  SearchDialogList,
  SearchDialogOverlay,
  useSearch as useDialog,
} from 'fumadocs-ui/components/dialog/search'
import { useI18n } from 'fumadocs-ui/contexts/i18n'
import type { SharedProps } from 'fumadocs-ui/contexts/search'
import type { Lang } from '~/lib/i18n'

// The index and its search code load with the first open, not with every page.
const engine = () => import('./search-engine')

export default function Search(props: SharedProps) {
  const lang = useI18n().locale as Lang
  const [run] = useState(() => async (q: string) => (await engine()).find(lang, q))
  const search = useSearch(run)
  const [start, setStart] = useState<SortedResult[] | null>(null)
  useEffect(() => {
    if (!props.open || start) return
    void engine()
      .then((e) => e.popularPages(lang))
      .then(setStart)
  }, [props.open, start, lang])
  return (
    <SearchDialog {...search} {...props}>
      <SearchDialogOverlay />
      <SearchDialogContent>
        <SearchDialogHeader>
          <SearchDialogIcon />
          {/* Without these, password managers take the bare text field in a dialog for a login field. */}
          <SearchDialogInput
            type="search"
            name="docs-query"
            autoComplete="off"
            autoCorrect="off"
            spellCheck={false}
            enterKeyHint="search"
            data-1p-ignore=""
            data-lpignore="true"
            data-bwignore=""
            data-form-type="other"
          />
          <SearchDialogClose />
        </SearchDialogHeader>
        <SearchDialogList defaultItems={start} />
        <BestHit />
      </SearchDialogContent>
    </SearchDialog>
  )
}

// The list makes its first row active, which is the page. Enter should open the best matching section of it.
function BestHit() {
  const { data, setActive } = useDialog()
  useLayoutEffect(() => {
    const next = data?.items[1]
    if (data?.items[0]?.type !== 'page' || !next || next.type === 'page') return
    setActive(next.type === 'table' ? (next.rows[0]?.id ?? null) : next.id)
  }, [data, setActive])
  return null
}
