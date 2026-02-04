#include <benchmark/benchmark.h>
#include <iostream>
#include <string>

using namespace std;

void demo()
{
    string str = "hello world";
    str.size();
}


static void BM_demo(benchmark::State& state) {
    benchmark::BenchmarkReporter::Run bmr;
    for (auto _ : state)
        demo();
}
// Register the function as a benchmark
BENCHMARK(BM_demo)->Iterations(1000);; //用于注册测试函数
// BENCHMARK_MAIN(); //程序入口
int main(int argc, char** argv) {                                     \
    ::benchmark::Initialize(&argc, argv);                               \
    if (::benchmark::ReportUnrecognizedArguments(argc, argv)) return 1; \
    ::benchmark::RunSpecifiedBenchmarks();                              \
    ::benchmark::Shutdown();                                            \
    return 0;                                                           \
  } 
  
  
                                                                      \
// #include <benchmark/benchmark.h>
// #include <cstring>

// static void BM_memcpy(benchmark::State& state) {
//     char* src = new char[state.range(0)];
//     char* dst = new char[state.range(0)];
//     memset(src, 'x', state.range(0));
//     for (auto _ : state)
//         memcpy(dst, src, state.range(0));
//     state.SetBytesProcessed(int64_t(state.iterations()) * int64_t(state.range(0)));
//     delete[] src;
//     delete[] dst;
// }
// BENCHMARK(BM_memcpy)->Arg(8)->Arg(64)->Arg(512)->Arg(1<<10)->Arg(8<<10);
// BENCHMARK_MAIN(); 
