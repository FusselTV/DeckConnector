import { DocPage, docMetadata, docParams } from '~/components/docs-page'

type Props = { params: Promise<{ slug?: string[] }> }

export default async function Page({ params }: Props) {
  return <DocPage lang="en" slug={(await params).slug} />
}

export const generateStaticParams = () => docParams('en')
export const generateMetadata = async ({ params }: Props) => docMetadata('en', (await params).slug)
