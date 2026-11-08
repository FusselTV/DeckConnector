import js from '@eslint/js'
import nextPlugin from '@next/eslint-plugin-next'
import jsxA11y from 'eslint-plugin-jsx-a11y'
import reactHooks from 'eslint-plugin-react-hooks'
import globals from 'globals'
import tseslint from 'typescript-eslint'

export default tseslint.config(
  { ignores: ['.next', '.source', 'out', 'public', 'next-env.d.ts'] },
  js.configs.recommended,
  ...tseslint.configs.strictTypeChecked,
  {
    files: ['**/*.tsx', '**/*.ts'],
    extends: [reactHooks.configs.flat.recommended, jsxA11y.flatConfigs.recommended],
    plugins: { '@next/next': nextPlugin },
    rules: {
      ...nextPlugin.configs.recommended.rules,
      ...nextPlugin.configs['core-web-vitals'].rules,
      // EN and DE are separate root layouts, so a link between them is a full page load either way.
      '@next/next/no-html-link-for-pages': 'off',
    },
  },
  {
    languageOptions: {
      globals: globals.browser,
      parserOptions: { projectService: true, tsconfigRootDir: import.meta.dirname },
    },
    rules: {
      '@typescript-eslint/restrict-template-expressions': ['error', { allowNumber: true }],
      '@typescript-eslint/no-confusing-void-expression': 'off',
      'react-hooks/preserve-manual-memoization': 'off',
      '@typescript-eslint/no-unused-vars': [
        'error',
        { varsIgnorePattern: '^_', argsIgnorePattern: '^_', ignoreRestSiblings: true },
      ],
    },
  },
  {
    files: ['*.js', '*.mjs', 'scripts/**'],
    extends: [tseslint.configs.disableTypeChecked],
    languageOptions: { globals: globals.node },
  },
)
