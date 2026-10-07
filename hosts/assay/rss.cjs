// Preloaded into the launcher's main thread by run.sh.  The assay worker
// shares the process, so process RSS covers it.  Kills the process above
// 4 GB and prints the peak RSS and wall time on exit.
// Source: escrow-lang 279214c, probe/rss.cjs.
const t0 = process.hrtime.bigint();
const cap = 4 * 1024 * 1024 * 1024;
const timer = setInterval(() => {
  if (process.memoryUsage.rss() > cap) {
    process.stderr.write('[probe] RSS above 4 GB, killed\n');
    process.exit(137);
  }
}, 100);
timer.unref();
process.on('exit', (code) => {
  const ms = Number(process.hrtime.bigint() - t0) / 1e6;
  const mb = Math.round(process.resourceUsage().maxRSS / 1024);
  process.stderr.write(`[probe] exit=${code} wall_s=${(ms / 1000).toFixed(2)} peak_rss_mb=${mb}\n`);
});
