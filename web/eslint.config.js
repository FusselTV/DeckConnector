import js from '@eslint/js'
import jsxA11y from 'eslint-plugin-jsx-a11y'
import reactHooks from 'eslint-plugin-react-hooks'
import globals from 'globals'
import tseslint from 'typescript-eslint'

export default tseslint.config(
  { ignores: ['dist', 'dist-demo', 'test-results', 'playwright-report'] },
  js.configs.recommended,
  ...tseslint.configs.strictTypeChecked,
  { files: ['src/**'], extends: [reactHooks.configs.flat.recommended, jsxA11y.flatConfigs.recommended] },
  {
    languageOptions: {
      globals: globals.browser,
      parserOptions: { projectService: true, tsconfigRootDir: import.meta.dirname },
    },
    rules: {
      '@typescript-eslint/restrict-template-expressions': ['error', { allowNumber: true }],
      '@typescript-eslint/no-confusing-void-expression': 'off',
      // The build does not run the React Compiler, so its memoization check reports nothing actionable.
      'react-hooks/preserve-manual-memoization': 'off',
      '@typescript-eslint/no-unused-vars': [
        'error',
        { varsIgnorePattern: '^_', argsIgnorePattern: '^_', ignoreRestSiblings: true },
      ],
    },
  },
  {
    files: ['*.js', 'scripts/**'],
    extends: [tseslint.configs.disableTypeChecked],
    languageOptions: { globals: globals.node },
  },
)
