// The simulator's and the dev server's ports, for playwright.config.ts and the fixtures alike. Two checkouts run their
// e2e tests side by side with DC_E2E_SIM and DC_E2E_WEB set to other ports.
export const SIM_PORT = Number(process.env.DC_E2E_SIM ?? 18210)
export const WEB_PORT = Number(process.env.DC_E2E_WEB ?? 5181)
