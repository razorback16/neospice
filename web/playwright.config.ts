import { defineConfig } from "@playwright/test";
export default defineConfig({
  testDir: "tests",
  testMatch: "browser.spec.ts",
  timeout: 30000,
  fullyParallel: false,
  workers: 1,
  reporter: "list",
  use: {
    baseURL: process.env.LAB_URL || "http://127.0.0.1:48173/",
    viewport: { width: 1512, height: 982 },
    launchOptions: {
      ...(process.env.CHROME_BINARY
        ? { executablePath: process.env.CHROME_BINARY }
        : {}),
      args: ["--no-sandbox"],
    },
    screenshot: "only-on-failure",
    trace: "retain-on-failure",
  },
});
