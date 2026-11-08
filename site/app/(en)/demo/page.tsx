import { DemoEmbed, demoText } from '~/components/demo/DemoEmbed'
import { pageMeta } from '~/lib/meta'

export const metadata = pageMeta('en', '/demo/', { title: demoText.en.title, description: demoText.en.description })

export default function Page() {
  return <DemoEmbed lang="en" />
}
