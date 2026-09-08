import autocannon from 'autocannon';

const start = performance.now();
async function runTesting() {
  const instance = autocannon(
    {
      title: 'Load testing for simple esp32 server',
      url: 'http://192.168.1.200/api/int-sys-info',
      connections: 7,
      pipelining: 1,
      amount: 10000,
      timeout: 60,
      method: 'GET',
    },
    (e, r) => {
      console.log(e);
      // console.log(r);
      console.log(JSON.stringify(r));
      console.log((performance.now() - start) / 1000);
    }
  );
  instance.on('reqError', (err) => {
    console.error('A request failed or timed out:', err);
  });
  autocannon.track(instance, { renderProgressBar: true });
}

await runTesting();
