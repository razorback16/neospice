import { createRoot } from "react-dom/client";
import { Component, type ErrorInfo, type ReactNode } from "react";
import App from "./App";
import "./style.css";
class ErrorBoundary extends Component<
  { children: ReactNode },
  { error: string }
> {
  state = { error: "" };
  static getDerivedStateFromError(error: Error) {
    return { error: error.message };
  }
  componentDidCatch(error: Error, info: ErrorInfo) {
    console.error(error, info);
  }
  render() {
    return this.state.error ? (
      <main className="fatal-error">
        <h1>The lab needs a fresh start.</h1>
        <p>{this.state.error}</p>
        <button
          onClick={() => {
            localStorage.removeItem("neospice-lab-v1");
            location.reload();
          }}
        >
          Reset local workspace
        </button>
        <p>Exported project files are unaffected.</p>
      </main>
    ) : (
      this.props.children
    );
  }
}
createRoot(document.getElementById("root")!).render(
  <ErrorBoundary>
    <App />
  </ErrorBoundary>,
);
