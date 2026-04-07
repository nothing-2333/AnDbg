#pragma once

// 单例模板基类
/* 使用方法
  * 1. 定义一个类继承自 SingletonBase<YourClassName>
  * 2. 将构造函数和析构函数设为 private 或 protected
  * 3. 在类内添加 friend class SingletonBase<YourClassName>; 允许基类访问私有构造函数
  * 4. 通过 YourClassName::get_instance() 获取单例实例
  * 
  * 例如:
  * class MySingleton : public SingletonBase<MySingleton> 
  * {
  *   friend class SingletonBase<MySingleton>; // 允许基类访问私有构造函数
  * private:
  *   friend class SingletonBase<YourClassName>; // 允许基类访问私有构造函数
  *   MySingleton() = default; // 私有构造函数
  *   ~MySingleton() = default; // 私有析构函数
  * public:
  *   void do_something() { ... }
  * };
  * 
  * MySingleton& instance = MySingleton::get_instance();
  * instance.do_something();
*/
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