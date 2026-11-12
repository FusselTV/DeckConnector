import { Contact } from '~/components/contact'
import { legalMeta, legalPath } from '~/components/legal'
import { owner, repo } from '~/site.config'

export const metadata = legalMeta('en', 'contact')

export default function Page() {
  return <Contact lang="en" email={owner.email} issues={`${repo}/issues`} privacy={legalPath.privacy.en} />
}
