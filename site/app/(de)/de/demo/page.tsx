import { DemoEmbed, demoText } from '~/components/demo/DemoEmbed'
import { pageMeta } from '~/lib/meta'

export const metadata = pageMeta('de', '/demo/', { title: demoText.de.title, description: demoText.de.description })

export default function Page() {
  return <DemoEmbed lang="de" />
}
