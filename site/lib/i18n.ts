import { defineI18n } from 'fumadocs-core/i18n'

export const i18n = defineI18n({ languages: ['en', 'de'], defaultLanguage: 'en', hideLocale: 'default-locale' })
export type Lang = (typeof i18n.languages)[number]
export const prefix = (lang: Lang) => (lang === 'en' ? '' : '/de')
