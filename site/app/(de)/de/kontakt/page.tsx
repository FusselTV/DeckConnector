import { Contact } from '~/components/contact'
import { legalMeta, legalPath } from '~/components/legal'
import { owner, repo } from '~/site.config'

export const metadata = legalMeta('de', 'contact')

export default function Page() {
  return <Contact lang="de" email={owner.email} issues={`${repo}/issues`} privacy={legalPath.privacy.de} />
}
