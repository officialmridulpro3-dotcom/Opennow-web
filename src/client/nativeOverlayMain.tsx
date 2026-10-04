import React from "react";
import ReactDOM from "react-dom/client";
import { NativeStreamOverlay } from "./components/NativeStreamOverlay";
import "./nativeStreamOverlay.css";

ReactDOM.createRoot(document.getElementById("root") as HTMLElement).render(
  <React.StrictMode>
    <NativeStreamOverlay />
  </React.StrictMode>,
);
