#pragma once

class Debugger
{
private:

public:
  Debugger(){};

  // 启动
  bool launch();

  // 附加
  bool attach();

  // 单步步入
  bool step_into();

  // 单步步过
  bool step_over();

  // 执行
  bool run();
};