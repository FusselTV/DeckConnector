import { loader } from 'fumadocs-core/source'
import { defineDocs } from 'fumadocs-mdx/macro'
import { i18n } from './i18n'

const docs = defineDocs()

export const source = loader(docs.toFumadocsSource(), { baseUrl: '/docs', i18n })
