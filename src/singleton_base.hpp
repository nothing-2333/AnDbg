#pragma once

// 单例模板基类
template <typename T>
class SingletonBase 
{
public:
  // 禁止拷贝构造、拷贝赋值、移动构造、移动赋值, 复用给所有子类
  SingletonBase(const SingletonBase&) = delete;
  SingletonBase& operator=(const SingletonBase&) = delete;
  SingletonBase(SingletonBase&&) = delete;
  SingletonBase& operator=(SingletonBase&&) = delete;

  // 静态获取实例接口
  static T& get_instance() 
  {
    static T instance;
    return instance;
  }

protected:
  // 保护构造/析构: 允许子类调用, 禁止外部直接创建基类对象
  SingletonBase() = default;
  ~SingletonBase() = default;
};