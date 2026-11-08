import { createI18nSearchAPI } from 'fumadocs-core/search/server'
import { i18n } from '~/lib/i18n'
import { source } from '~/lib/source'

export const revalidate = false

export const { staticGET: GET } = createI18nSearchAPI('advanced', {
  i18n,
  indexes: () =>
    source.getLanguages().flatMap(({ language, pages }) =>
      pages.map((page) => ({
        id: page.url,
        title: page.data.title,
        description: page.data.description,
        url: page.url,
        locale: language,
        structuredData: page.data.structuredData,
      })),
    ),
})
