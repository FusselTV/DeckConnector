'use client'
import { useStaticSearch } from 'fumadocs-core/search/client/orama-static'
import { SearchDialog, SearchDialogClose, SearchDialogContent, SearchDialogHeader, SearchDialogIcon, SearchDialogInput, SearchDialogList, SearchDialogOverlay } from 'fumadocs-ui/components/dialog/search'
import { useI18n } from 'fumadocs-ui/contexts/i18n'
import type { SharedProps } from 'fumadocs-ui/contexts/search'

export default function Search(props: SharedProps) {
  const { locale } = useI18n()
  const search = useStaticSearch({ locale })
  return (
    <SearchDialog {...search} {...props}>
      <SearchDialogOverlay />
      <SearchDialogContent>
        <SearchDialogHeader>
          <SearchDialogIcon />
          <SearchDialogInput />
          <SearchDialogClose />
        </SearchDialogHeader>
        <SearchDialogList />
      </SearchDialogContent>
    </SearchDialog>
  )
}
