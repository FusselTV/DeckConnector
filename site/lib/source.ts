import { loader } from 'fumadocs-core/source'
import { remarkStructureDefaultOptions } from 'fumadocs-core/mdx-plugins'
import { applyMdxPreset } from 'fumadocs-mdx/config'
import { defineDocs } from 'fumadocs-mdx/macro'
import { i18n } from './i18n'
import { remarkVersion } from './version'

const docs = defineDocs({
  docs: {
    mdxOptions: applyMdxPreset({
      remarkPlugins: [remarkVersion],
      remarkStructureOptions: { types: [...remarkStructureDefaultOptions.types, 'code'] },
    }),
  },
})

export const source = loader(docs.toFumadocsSource(), { baseUrl: '/docs', i18n })
