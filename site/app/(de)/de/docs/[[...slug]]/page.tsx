import { DocPage, docMetadata, docParams } from '~/components/docs-page'

type Props = { params: Promise<{ slug?: string[] }> }

export default async function Page({ params }: Props) {
  return <DocPage lang="de" slug={(await params).slug} />
}

export const generateStaticParams = () => docParams('de')
export const generateMetadata = async ({ params }: Props) => docMetadata('de', (await params).slug)
