import autocannon from 'autocannon';

const start = performance.now();
const testsQuantityInApi = 500;
async function runTesting() {

  const responseSize = 1024 * 100; // up to 200k
  const instance1 = autocannon(
    {
      title: 'Load testing for simple esp32 server',
      url: `http://192.168.1.200/api/performance-testing?responseSize=${responseSize}`,
      // url: `http://192.168.4.1/api/performance-testing?responseSize=${responseSize}`,
      connections: 7,
      pipelining: 1,
      amount: testsQuantityInApi,
      timeout: 60,
    },
    (e, r) => {
      console.log(e);
      console.log(JSON.stringify(r));
      console.log((performance.now() - start) / 1000);
    }
  );
  instance1.on('reqError', (err) => {
    console.error('A request failed or timed out:', err);
  });
  autocannon.track(instance1, { renderProgressBar: true });
}

await runTesting();
