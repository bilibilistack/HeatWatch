import type { Metadata, Viewport } from "next";
import "./globals.css";

export const metadata: Metadata = {
  title: "HeatWatch | 矿区防工伤与热应激安全中控台",
  description: "HeatWatch 是一款面向西澳采矿及高温高空作业环境的物联网智能安全中控台，实时展示心率、体表温度、环境 WBGT 指标，实时预警坠落工伤与热应激风险。",
};

export const viewport: Viewport = {
  width: "device-width",
  initialScale: 1,
  maximumScale: 1,
  userScalable: false,
};

export default function RootLayout({
  children,
}: Readonly<{
  children: React.ReactNode;
}>) {
  return (
    <html lang="zh-CN">
      <body>{children}</body>
    </html>
  );
}
