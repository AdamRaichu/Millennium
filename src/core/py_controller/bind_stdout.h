/**
 * bind_stdout.h
 * @brief This file is responsible for redirecting the Python stdout and stderr to the logger.
 * @note You shouldn't rely print() in python, use "PluginUtils" utils module, this is just a catch-all for any print() calls that may be made.
 */

#include <iostream>
#include <Python.h>
#include "co_spawn.h"
#include <sys/log.h>
#include <fmt/core.h>
#include "logger.h"

extern "C" void PrintPythonMessage(std::string pname, const char* message) 
{
    const std::string logMessage = message;

    if (logMessage != "\n" && logMessage != " ")
    {
        Logger.LogPluginMessage(pname, message);
        InfoToLogger(pname, message);
    }
}

extern "C" void PrintPythonError(std::string pname, const char* message)
{
    fmt::print(fg(fmt::color::red), "{}", message);
    ErrorToLogger(pname, message);
}

extern "C" 
{
    static PyObject* CustomStdoutWrite(PyObject* self, PyObject* args) 
    {
        const char* message;
        if (!PyArg_ParseTuple(args, "s", &message)) 
        { 
            return NULL; 
        }

        PrintPythonMessage(PythonManager::GetInstance().GetPluginNameFromThreadState(PyThreadState_Get()), message);
        return Py_BuildValue("");
    }

    static PyObject* CustomStderrWrite(PyObject* self, PyObject* args) 
    {
        const char* message;
        if (!PyArg_ParseTuple(args, "s", &message)) 
        {
            return NULL; 
        }

        PrintPythonError(PythonManager::GetInstance().GetPluginNameFromThreadState(PyThreadState_Get()), message);
        return Py_BuildValue("");
    }

    static PyMethodDef module_methods[] = 
    {
        {"write", CustomStdoutWrite, METH_VARARGS, "Custom stdout write function"}, {NULL, NULL, 0, NULL}
    };

    static PyMethodDef stderr_methods[] = 
    {
        {"write", CustomStderrWrite, METH_VARARGS, "Custom stderr write function"}, {NULL, NULL, 0, NULL}
    };

    static struct PyModuleDef custom_stdout_module = { PyModuleDef_HEAD_INIT, "hook_stdout", NULL, -1, module_methods };
    static struct PyModuleDef custom_stderr_module = { PyModuleDef_HEAD_INIT, "hook_stderr", NULL, -1, stderr_methods };

    PyObject* PyInit_CustomStderr(void) 
    {
        return PyModule_Create(&custom_stderr_module);
    }

    PyObject* PyInit_CustomStdout(void) 
    {
        return PyModule_Create(&custom_stdout_module);
    }

    const void RedirectOutput() 
    {
        PyObject* sys = PyImport_ImportModule("sys");

        PyObject_SetAttrString(sys, "stdout", PyImport_ImportModule("hook_stdout"));
        PyObject_SetAttrString(sys, "stderr", PyImport_ImportModule("hook_stderr"));
    }
}